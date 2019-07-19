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

from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import grumpy.bridge
import grumpy.model
import grumpy.view

import bucks.app.base

from bucks.datamodel.account import Account
from bucks.datamodel.category import Category
from bucks.datamodel.transaction import Transaction


class CategoryForm(bucks.app.base.BucksForm):
    def __init__(self, tab):
        super(CategoryForm, self).__init__(tab, Category)
        self.addProperty(Category, "^", 0, 1, refclass=Category,
                         label="Subcategory of", required=False)
        self.addProperty(Category, "cat_name", 2, 1)
        self.addProperty(Category, "description", 3, 1)
        self.addProperty(Category, "current_balance", 4, 1)
        q = Transaction.query(keys_only=False).add_parent_join(Account)
        self.tx_list = grumpy.view.TableView(q, ["date", "+p:account.acc_name", "credit", "debit", "description"])
        self.addTab(self.tx_list, "Transactions")
        self.subcategories = grumpy.view.TableView(Category.query(keys_only=False), ["cat_name"])
        self.addTab(self.subcategories, "Subcategories")
        QCoreApplication.instance().importer.imported.connect(self.tx_list.refresh)

    def assigned(self, key):
        self.tx_list.query().clear_filters()
        self.tx_list.query().add_filter("category", "->", self.instance())
        self.tx_list.refresh()
        self.subcategories.query().set_parent(self.instance())
        self.subcategories.refresh()

    def new_instance(self):
        pass


class CategoryTab(QWidget):
    def __init__(self, parent):
        super(CategoryTab, self).__init__(parent=parent)
        layout = QVBoxLayout(self)
        q = Transaction.query(keys_only=False, include_subclasses=True, alias="category")
        q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
        q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
        q.add_aggregate("k.debit", name="total_debit", groupby=Category, func="SUM")
        q.add_aggregate("k.credit", name="total_credit", groupby=Category, func="SUM")
        q.add_aggregate("k.amt", name="total", groupby=Category, func="SUM")
        q.add_join(Category, "category", jointype="RIGHT")
        self.tree = grumpy.view.TreeView(self, kind=Category, query=q, root=None, columns=[
                                            bucks.app.base.MoneyColumn("total_debit", "Debit"),
                                            bucks.app.base.MoneyColumn("total_credit", "Credit"),
                                            bucks.app.base.MoneyColumn("total", "Balance"),
                                            bucks.app.base.MoneyColumn("cum_debit", "Total Debit"),
                                            bucks.app.base.MoneyColumn("cum_credit", "Total Credit"),
                                            bucks.app.base.MoneyColumn("cum_total", "Cum.Balance"),
                                         ], itemclass=bucks.app.base.BucksTreeItem)
        self.tree.setMinimumSize(400, 300)
        layout.addWidget(self.tree)
        self.form = CategoryForm(self)
        layout.addWidget(self.form)
        self.setLayout(layout)
        self.tree.objectSelected.connect(self.form.set_instance)
        self.form.refresh.connect(self.tree.refresh)
        QCoreApplication.instance().importer.imported.connect(self.tree.refresh)
