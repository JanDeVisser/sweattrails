#
# Copyright (c) 2015 Jan de Visser (jan@sweattrails.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtCore import Qt

from PyQt5.QtWidgets import QGroupBox
from PyQt5.QtWidgets import QHBoxLayout
from PyQt5.QtWidgets import QPushButton
from PyQt5.QtWidgets import QTabWidget
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe
import grumble.qt.bridge
import grumble.qt.model
import grumble.qt.view
import sweattrails.config
import sweattrails.qt.async.bg
import sweattrails.qt.graphs
import sweattrails.qt.stackedpage
import sweattrails.qt.view
import sweattrails.session

logger = gripe.get_logger(__name__)


class BestPaceList(grumble.qt.view.TableView):
    def __init__(self, parent, cpdef, user=None):
        super(BestPaceList, self).__init__(parent=parent)
        self.cpdef = cpdef
        self.setQueryAndColumns(parent.query,
                                grumble.qt.model.TableColumn('+session."start_time"', format="%A %B %d", header="Date"),
                                sweattrails.qt.view.DistanceColumn("atdistance", header="At distance"),
                                sweattrails.qt.view.PaceSpeedColumn(what="Pace"))
        self.setMinimumHeight(150)
        QCoreApplication.instance().refresh.connect(self.refresh)

    def resetQuery(self):
        self.query().set_parent(self.cpdef)


class RunProgressionAxis(sweattrails.qt.graphs.QueryDataSource, sweattrails.qt.graphs.DateAxis):
    def __init__(self, tab):
        super(RunProgressionAxis, self).__init__(query=tab.query,
                                                 value=lambda runpace: runpace.joined_value('session."start_time"'))


class CriticalPaceTab(QWidget):
    def __init__(self, parent, cpdef, user=None):
        super(CriticalPaceTab, self).__init__(parent)
        self.cpdef = cpdef
        if not user:
            user = QCoreApplication.instance().user
        self.query = sweattrails.session.RunPace.get_progression(cpdef, user)
        layout = QVBoxLayout(self)
        progression = RunProgressionAxis(self)
        self.graphs = sweattrails.qt.graphs.Graph(self, progression)
        sweattrails.qt.graphs.Series(property="speed", color=Qt.red, graph=self.graphs)
        layout.addWidget(self.graphs)
        self.list = BestPaceList(self, cpdef)
        layout.addWidget(self.list)


class RunFitnessPage(QWidget):
    def __init__(self, parent = None):
        super(RunFitnessPage, self).__init__(parent)
        layout = QHBoxLayout(self)
        self.tabs = QTabWidget(self)
        layout.addWidget(self.tabs)
        user = QCoreApplication.instance().user
        if user:
            profile = sweattrails.config.ActivityProfile.get_profile(user)
            for cpdef in profile.get_all_linked_references(sweattrails.config.CriticalPace):
                self.tabs.addTab(CriticalPaceTab(self, cpdef), cpdef.name)
        self.setMinimumSize(800, 600)


class BestPowerList(grumble.qt.view.TableView):
    def __init__(self, parent, cpdef, user=None):
        super(BestPowerList, self).__init__(parent=parent)
        self.cpdef = cpdef
        if not user:
            user = QCoreApplication.instance().user
        query = sweattrails.session.CriticalPower.get_progression(cpdef, user)
        self.setQueryAndColumns(query,
                                grumble.qt.model.TableColumn('+session."start_time"', format="%A %B %d", header="Date"),
                                grumble.qt.model.TableColumn('+cpdef."name"', header="Duration"),
                                grumble.qt.model.TableColumn("power", header="Power"))
        self.setMinimumHeight(150)
        QCoreApplication.instance().refresh.connect(self.refresh)

    def resetQuery(self):
        self.query().set_parent(self.cpdef)


class CriticalPowerTab(QWidget):
    def __init__(self, parent, cpdef):
        super(CriticalPowerTab, self).__init__(parent)
        self.cpdef = cpdef
        layout = QVBoxLayout(self)
        self.list = BestPowerList(self, cpdef)
        layout.addWidget(self.list)


class BikeFitnessPage(QWidget):
    def __init__(self, parent = None):
        super(BikeFitnessPage, self).__init__(parent)
        layout = QHBoxLayout(self)
        self.tabs = QTabWidget(self)
        layout.addWidget(self.tabs)
        user = QCoreApplication.instance().user
        if user:
            profile = sweattrails.config.ActivityProfile.get_profile(user)
            for cpdef in profile.get_all_linked_references(sweattrails.config.CriticalPowerInterval):
                self.tabs.addTab(CriticalPowerTab(self, cpdef), cpdef.name)
        self.setMinimumSize(800, 600)

    def activate(self):
        pass
    
    
class WeightList(grumble.qt.view.TableView):
    def __init__(self, parent):
        super(WeightList, self).__init__(parent = parent)
        query = sweattrails.userprofile.WeightHistory.query(keys_only = False)
        query.add_sort("snapshotdate",  False)
        self.setQueryAndColumns(query,
                grumble.qt.model.TableColumn("snapshotdate", format = "%A %B %d %Y", header = "Date"),
                grumble.qt.model.TableColumn("weight"),
                grumble.qt.model.TableColumn("bmi", header = "BMI"),
                grumble.qt.model.TableColumn("bfPercentage", header = "Body fat %"),
                grumble.qt.model.TableColumn("waist"))
        self.setMinimumHeight(150)
        QCoreApplication.instance().refresh.connect(self.refresh)

    def resetQuery(self):
        user = QCoreApplication.instance().user
        part = user.get_part("WeightMgmt")
        self.query().set_parent(part)
        

class WeightAxis(sweattrails.qt.graphs.QueryDataSource, sweattrails.qt.graphs.DateAxis):
    def __init__(self):
        user = QCoreApplication.instance().user
        part = user.get_part("WeightMgmt")
        query = sweattrails.userprofile.WeightHistory.query(keys_only=False, parent=part).add_sort("snapshotdate")
        super(WeightAxis, self).__init__(query=query, property="snapshotdate")


class WeightPage(QWidget):
    def __init__(self, parent = None):
        super(WeightPage, self).__init__(parent)
        self.setMinimumSize(800, 600)
        layout = QVBoxLayout(self)
        self.graphs = None

        weightAxis = WeightAxis()
        self.graphs = sweattrails.qt.graphs.Graph(self, weightAxis)
        sweattrails.qt.graphs.Series(property="weight", color=Qt.red, graph=self.graphs)
        bmi = sweattrails.qt.graphs.Series(property="bmi", color=Qt.blue, graph=self.graphs, min=10, max=30)
        bmi.addTrendLine(25)
        bmi.addTrendLine(15)
        # bf = sweattrails.qt.graphs.Series(property="bfPercentage", color=Qt.magenta, graph=self.graphs)
        # if bf.max() == 0.0:
        #     bf.hide()
        # waist = sweattrails.qt.graphs.Series(property="waist", color=Qt.darkGreen, graph=self.graphs)
        # if waist.max() == 0.0:
        #     waist.hide()

        layout.addWidget(self.graphs)

        self.list = WeightList(self)
        layout.addWidget(self.list)
        buttonWidget = QGroupBox()
        self.buttonbox = QHBoxLayout(buttonWidget)
        self.addbutton = QPushButton("Add", self)
        self.addbutton.clicked.connect(self.addWeightEntry)
        self.buttonbox.addWidget(self.addbutton)
        self.withingsbutton = QPushButton("Download Withings Data", self)
        self.withingsbutton.clicked.connect(self.withingsDownload)
        self.buttonbox.addWidget(self.withingsbutton)
        layout.addWidget(buttonWidget)

    def activate(self):
        pass

    def withingsDownload(self):
        job = sweattrails.withings.WithingsJob()
        job.jobFinished.connect(self.list.refresh)
        sweattrails.qt.async.bg.BackgroundThread.add_backgroundjob(job)
        
    def addWeightEntry(self):
        pass


class FitnessTab(sweattrails.qt.stackedpage.StackedPage):
    def __init__(self, parent = None):
        super(FitnessTab, self).__init__(parent)

    def activate(self, ix):
        self.addPage("Run Fitness", RunFitnessPage(self))
        self.addPage("Bike Fitness", BikeFitnessPage(self))
        self.addPage("Weight", WeightPage(self))
