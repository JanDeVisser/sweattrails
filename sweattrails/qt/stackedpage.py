# To change this license header, choose License Headers in Project Properties.
# To change this template file, choose Tools | Templates
# and open the template in the editor.

__author__="jan"
__date__ ="$30-Jul-2014 8:36:09 PM$"

from PyQt5.QtWidgets import QButtonGroup
from PyQt5.QtWidgets import QHBoxLayout
from PyQt5.QtWidgets import QGroupBox
from PyQt5.QtWidgets import QPushButton
from PyQt5.QtWidgets import QStackedWidget
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

class StackedPage(QWidget):
    def __init__(self, parent = None):
        super(StackedPage, self).__init__(parent)
        layout = QHBoxLayout(self)
        leftpane = QVBoxLayout()
        self.buttongroup = QButtonGroup(self)
        self.buttongroup.setExclusive(True)
        self.groupbox = QGroupBox(self)
        self.groupbox.setMinimumWidth(200)
        QVBoxLayout(self.groupbox)
        leftpane.addWidget(self.groupbox)
        leftpane.addStretch(1)
        layout.addLayout(leftpane)
        self.rightpane = QStackedWidget(self)
        layout.addWidget(self.rightpane)
        self.buttongroup.buttonClicked[int].connect(self.rightpane.setCurrentIndex)
        self.rightpane.currentChanged[int].connect(self.activate)
         
    def addPage(self, buttontext, widget):
        button = QPushButton(buttontext)
        button.setCheckable(True)
        button.setChecked(self.rightpane.count() == 0)
        self.buttongroup.addButton(button, self.rightpane.count())
        self.groupbox.layout().addWidget(button)
        self.rightpane.addWidget(widget)
        
    def pages(self):
        return [ self.rightpane.widget(i) for i in range(self.rightpane.count()) ]

    def activate(self, ix):
        page = self.rightpane.currentWidget()
        if hasattr(page, "activate") and callable(page.activate):
            page.activate()
            
    def showEvent(self, *args, **kwargs):
        self.activate(0)
        return QWidget.showEvent(self, *args, **kwargs)