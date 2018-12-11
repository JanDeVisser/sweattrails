'''
Created on Jul 29, 2014

@author: jan
'''

if __name__ == '__main__':
    import sys

    from PyQt5.QtWidgets import QAction
    from PyQt5.QtWidgets import QApplication
    from PyQt5.QtWidgets import QComboBox
    from PyQt5.QtWidgets import QHBoxLayout
    from PyQt5.QtWidgets import QLabel
    from PyQt5.QtWidgets import QMainWindow
    from PyQt5.QtWidgets import QPushButton
    from PyQt5.QtWidgets import QVBoxLayout
    from PyQt5.QtWidgets import QWidget

    import grumble.model
    import grumble.property
    import grumble.qt.model
    import grumble.qt.view

    class QtCountry(grumble.model.Model):
        _template_dir = "grumble/qt"
        countryname = grumble.property.TextProperty(verbose_name = "Country name", is_label = True)
        countrycode = grumble.property.TextProperty(is_key = True, verbose_name = "ISO Code")


    class QtUser(grumble.model.Model):
        _template_dir = "grumble/qt"
        email = grumble.property.TextProperty(is_key = True)
        display_name = grumble.property.TextProperty(required = True, is_label = True)


    class STMainWindow(QMainWindow):
        def __init__(self):
            QMainWindow.__init__(self)
            fileMenu = self.menuBar().addMenu(self.tr("&File"))
            fileMenu.addAction(
                QAction("E&xit", self,
                        shortcut = "Ctrl+Q",
                        statusTip = "Exit",
                        triggered = self.close))
            window = QWidget()
            layout = QVBoxLayout(self)
            l = QHBoxLayout()
            l.addWidget(self.createCombo())
            self.button = QPushButton("Pick Me")
            self.button.clicked.connect(self.set_user_id)
            l.addWidget(self.button)
            layout.addLayout(l)
            self.user_id = QLabel()
            layout.addWidget(self.user_id)
            layout.addWidget(self.createTable())
            window.setLayout(layout)
            self.setCentralWidget(window)

        def set_user_id(self):
            self.user_id.setText(self.combo.itemData(self.combo.currentIndex()).name)

        def createCombo(self):
            self.combo = QComboBox()
            view = grumble.qt.model.ListModel(grumble.Query(QtUser, False), "display_name")
            self.combo.setModel(view)
            return self.combo

        def createTable(self):
            tv = grumble.qt.view.TableView(QtCountry.query(keys_only = False), ["countryname", "countrycode"])
            tv.setMinimumSize(400, 300)
            return tv

    class SweatTrails(QApplication):
        def __init__(self, argv):
            super(SweatTrails, self).__init__(argv)

    app = SweatTrails(sys.argv)

    w = STMainWindow()
    w.show()
    app.exec_()
    sys.exit()

