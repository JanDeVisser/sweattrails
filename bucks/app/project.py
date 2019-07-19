#
#   Copyright (c) 2019 Jan de Visser (jan@sweattrails.com)
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the Free
#   Software Foundation; either version 2 of the License, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#   more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc., 51
#   Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtCore import Qt

from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import grumpy.bridge
import grumpy.model
import grumpy.view

import bucks.app.base

from bucks.datamodel.account import Transaction
from bucks.datamodel.project import Project


class ProjectForm(bucks.app.base.BucksForm):
    def __init__(self, tab):
        super(ProjectForm, self).__init__(tab, Project)
        self.addProperty(Project, "^", 0, 1, refclass=Project, label="Subproject of", required=False)
        self.addProperty(Project, "proj_name", 2, 1)
        self.addProperty(Project, "description", 3, 1)
        self.addProperty(Project, "category", 4, 1)
        self.sub_label = QLabel("Subprojects:")
        self.addWidget(self.sub_label, 5, 1, 1, 1, Qt.AlignTop)
        self.subprojects = grumpy.view.TableView(Project.query(keys_only=False),
                                                 ["proj_name"])
        self.addWidget(self.subprojects, 5, 2)

    def assigned(self, key):
        self.subprojects.show()
        self.sub_label.show()
        self.subprojects.query().set_parent(self.instance())
        self.subprojects.refresh()

    def new_instance(self):
        self.subprojects.hide()
        self.sub_label.hide()


class ProjectTab(QWidget):
    def __init__(self, parent):
        super(ProjectTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)

        q = Transaction.query(keys_only=False, include_subclasses=True, alias="project")
        q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
        q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
        q.add_aggregate("k.debit", name="total_debit", groupby=Project, func="SUM")
        q.add_aggregate("k.credit", name="total_credit", groupby=Project, func="SUM")
        q.add_aggregate("k.amt", name="total", groupby=Project, func="SUM")
        q.add_join(Project, "project", jointype="RIGHT")
        self.tree = grumpy.view.TreeView(self, kind=Project, query=q, root=None, columns=[
                                            grumpy.model.TableColumn("total_debit", header="Debit"),
                                            grumpy.model.TableColumn("total_credit", header="Credit"),
                                            grumpy.model.TableColumn("total", header="Total"),
                                         ])
        self.tree.setMinimumSize(400, 300)
        layout.addWidget(self.tree)
        self.form = ProjectForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.tree.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.tree.refresh)
        QCoreApplication.instance().importer.imported.connect(self.tree.refresh)

