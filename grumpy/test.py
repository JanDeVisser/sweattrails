'''
Created on Jul 29, 2014

@author: jan
'''

if __name__ == '__main__':
    import sys

    from PyQt5.QtCore import QCoreApplication

    from PyQt5.QtWidgets import QAction
    from PyQt5.QtWidgets import QApplication
    from PyQt5.QtWidgets import QComboBox
    from PyQt5.QtWidgets import QHBoxLayout
    from PyQt5.QtWidgets import QLabel
    from PyQt5.QtWidgets import QMainWindow
    from PyQt5.QtWidgets import QMessageBox
    from PyQt5.QtWidgets import QProgressBar
    from PyQt5.QtWidgets import QPushButton
    from PyQt5.QtWidgets import QTabWidget
    from PyQt5.QtWidgets import QVBoxLayout
    from PyQt5.QtWidgets import QWidget

    import grumble.model
    import grumble.property
    import grumpy.bridge
    import grumpy.model
    import grumpy.view

    class QtCountry(grumble.model.Model, template_dir="grumble/qt"):
        country_name = grumble.property.TextProperty(verbose_name="Country name", is_label=True)
        country_code = grumble.property.TextProperty(is_key=True, verbose_name="ISO Code")


    class QtUser(grumble.model.Model, template_dir="grumble/qt"):
        email = grumble.property.TextProperty(is_key=True)
        display_name = grumble.property.TextProperty(required=True, is_label=True)


    class UserForm(grumpy.bridge.FormWidget):
        def __init__(self, tab):
            super(UserForm, self).__init__(tab, grumpy.bridge.FormButtons.AllButtons)
            self.addProperty(QtUser, "email", 0, 1, readonly=True)
            self.addProperty(QtUser, "display_name", 1, 1)
            self.statusMessage.connect(QCoreApplication.instance().status_message)
            self.exception.connect(QCoreApplication.instance().status_message)
            self.instanceSaved.connect(QCoreApplication.instance().status_message)
            self.instanceSaved.connect(self.saved)
            self.instanceDeleted.connect(QCoreApplication.instance().status_message)
            self.set_instance(None)

        def saved(self, key):
            self.parent().refresh()


    class UserTab(QWidget):
        def __init__(self, window):
            super(UserTab, self).__init__(parent=window)
            layout = QVBoxLayout(self)
            hl = QHBoxLayout()
            self.combo = QComboBox()
            view = grumpy.model.ListModel(grumble.Query(QtUser, False), "display_name")
            self.combo.setModel(view)
            hl.addWidget(self.combo)
            self.button = QPushButton("Pick Me")
            self.button.clicked.connect(self.set_user_id)
            hl.addWidget(self.button)
            layout.addLayout(hl)
            self.user_form = UserForm(self)
            layout.addWidget(self.user_form)
            self.setLayout(layout)

        def set_user_id(self):
            cix = self.combo.currentIndex()
            data = self.combo.itemData(cix)
            self.user_form.set_instance(data)

        def refresh(self):
            view = grumpy.model.ListModel(grumble.Query(QtUser, False), "display_name")
            self.combo.setModel(view)


    class TestMainWindow(QMainWindow):
        def __init__(self):
            super(TestMainWindow, self).__init__()
            self.table = None
            file_menu = self.menuBar().addMenu(self.tr("&File"))
            file_menu.addAction(
                QAction("E&xit", self, shortcut="Ctrl+Q", statusTip="Exit", triggered=self.close))
            window = QWidget()
            layout = QVBoxLayout(self)
            self.tabs = QTabWidget()
            self.tabs.addTab(self.create_table(), "Countries")
            self.tabs.addTab(UserTab(self), "Users")
            layout.addWidget(self.tabs)
            window.setLayout(layout)
            self.message_label = QLabel()
            self.message_label.setMinimumWidth(200)
            self.statusBar().addPermanentWidget(self.message_label)
            self.progressbar = QProgressBar()
            self.progressbar.setMinimumWidth(100)
            self.progressbar.setMinimum(0)
            self.progressbar.setMaximum(100)
            self.statusBar().addPermanentWidget(self.progressbar)
            self.setCentralWidget(window)

        def create_table(self):
            self.table = grumpy.view.TableView(QtCountry.query(keys_only=False), ["country_name", "country_code"])
            self.table.setMinimumSize(400, 300)
            return self.table

        def status_message(self, msg, *args):
            self.message_label.setText(str(msg).format(*args))

        def error_message(self, msg, e):
            if e:
                msg = str(e) if not msg else "%s: %s" % (msg, str(e))
            if not msg:
                msg = "Unknown error"
            QMessageBox.error(self, "Error", msg)

        def progress_init(self, msg, *args):
            self.progressbar.setValue(0)
            self.status_message(msg, *args)

        def progress(self, percentage):
            self.progressbar.setValue(percentage)

        def progress_done(self):
            self.progressbar.reset()


    class QtGrumbleTest(QApplication):
        def __init__(self, argv):
            super(QtGrumbleTest, self).__init__(argv)
            self.main_window = None

        def start(self):
            self.main_window = TestMainWindow()
            self.main_window.show()

        def status_message(self, msg, *args):
            self.main_window.status_message(msg, *args)

        def progress_init(self, msg, *args):
            self.main_window.progress_init(msg, *args)

        def progress(self, percentage):
            self.main_window.progress(percentage)

        def progress_done(self):
            self.main_window.progress_done()


    app = QtGrumbleTest(sys.argv)
    app.start()

    app.exec_()
    sys.exit()
