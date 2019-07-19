import sys
import re

from PyQt5.QtCore import pyqtSlot
from PyQt5.QtCore import QItemSelectionModel
from PyQt5.QtCore import QModelIndex
# from PyQt5.QtCore imp QStringListModel
from PyQt5.QtCore import Qt

from PyQt5.QtGui import QCursor
from PyQt5.QtGui import QStandardItem
from PyQt5.QtGui import QStandardItemModel

from PyQt5.QtWidgets import QAction
from PyQt5.QtWidgets import QApplication
from PyQt5.QtWidgets import QCheckBox
from PyQt5.QtWidgets import QComboBox
from PyQt5.QtWidgets import QCompleter
from PyQt5.QtWidgets import QGridLayout
from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QLineEdit
from PyQt5.QtWidgets import QMainWindow
from PyQt5.QtWidgets import QMessageBox
from PyQt5.QtWidgets import QSizePolicy
from PyQt5.QtWidgets import QTreeView
from PyQt5.QtWidgets import QWidget


def tr(s):
    return s


class TreeModelCompleter(QCompleter):
    def __init__(self, model, parent):
        super(TreeModelCompleter, self).__init__(model, parent)
        self._sep = None

    def set_separator(self, separator):
        self._sep = separator

    def separator(self):
        return self._sep

    def splitPath(self, path):
        if self._sep is None:
            return super(TreeModelCompleter, self).splitPath(path)
        else:
            return path.split(self._sep)

    def pathFromIndex(self, index):
        if self._sep is None:
            return super(TreeModelCompleter, self).pathFromIndex(index)
        # navigate up and accumulate data
        data_list = []
        i = index
        while i.isValid():
            data_list.insert(0, str(self.model().data(i, self.completionRole())))
            i = i.parent()
        return self._sep.join(data_list)


class MainWindow(QMainWindow):
    def __init__(self, parent=None):
        super(MainWindow, self).__init__(parent)
        self._app = QApplication.instance()
        self._lineEdit = None
        self.create_menu()
        self._completer = TreeModelCompleter(None, self)
        self._completer.setModel(self.model_from_file("./treemodel.txt"))
        self._completer.set_separator(".")
        self._completer.highlighted[QModelIndex].connect(self.highlight)
        
        central_widget = QWidget()
        model_label = QLabel()
        model_label.setText("Tree Model<br>(Double click items to edit)")
        mode_label = QLabel()
        mode_label.setText("Completion Mode")
        self._mode_combo = QComboBox()
        self._mode_combo.addItem(tr("Inline"))
        self._mode_combo.addItem(tr("Filtered Popup"))
        self._mode_combo.addItem(tr("Unfiltered Popup"))
        self._mode_combo.setCurrentIndex(1)

        case_label = QLabel()
        case_label.setText(tr("Case Sensitivity"))
        self._case_combo = QComboBox()
        self._case_combo.addItem(tr("Case Insensitive"))
        self._case_combo.addItem(tr("Case Sensitive"))
        self._case_combo.setCurrentIndex(0)

        separator_label = QLabel()
        separator_label.setText(tr("Tree Separator"))

        separator_line_edit = QLineEdit()
        separator_line_edit.setText(self._completer.separator())
        separator_line_edit.textChanged.connect(self._completer.set_separator)

        wrap_check_box = QCheckBox()
        wrap_check_box.setText(tr("Wrap around completions"))
        wrap_check_box.setChecked(self._completer.wrapAround())
        wrap_check_box.clicked.connect(self._completer.setWrapAround)

        self._contents_label = QLabel()
        self._contents_label.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)
        separator_line_edit.textChanged.connect(self.update_contents_label)

        self._tree_view = QTreeView()
        self._tree_view.setModel(self._completer.model())
        self._tree_view.header().hide()
        self._tree_view.expandAll()

        self._mode_combo.activated.connect(self.change_mode)
        self._case_combo.activated.connect(self.change_case)

        self._line_edit = QLineEdit()
        self._line_edit.setCompleter(self._completer)
    
        layout = QGridLayout()
        layout.addWidget(model_label, 0, 0),     layout.addWidget(self._tree_view, 0, 1)
        layout.addWidget(mode_label, 1, 0),      layout.addWidget(self._mode_combo, 1, 1)
        layout.addWidget(case_label, 2, 0),      layout.addWidget(self._case_combo, 2, 1)
        layout.addWidget(separator_label, 3, 0), layout.addWidget(separator_line_edit, 3, 1)
        layout.addWidget(wrap_check_box, 4, 0)
        layout.addWidget(self._contents_label, 5, 0, 1, 2)
        layout.addWidget(self._line_edit, 6, 0, 1, 2)
        central_widget.setLayout(layout)
        self.setCentralWidget(central_widget)
    
        self.change_case(self._case_combo.currentIndex())
        self.change_mode(self._mode_combo.currentIndex())

        self.setWindowTitle(tr("Tree Model Completer"))
        self._line_edit.setFocus()

    def create_menu(self):
        exit_action = QAction(tr("Exit"), self)
        about_act = QAction(tr("About"), self)
        about_qt_act = QAction(tr("About Qt"), self)

        exit_action.triggered.connect(QApplication.instance().quit)
        about_act.triggered.connect(self.about)
        about_qt_act.triggered.connect(QApplication.instance().aboutQt)

        file_menu = self.menuBar().addMenu(tr("File"))
        file_menu.addAction(exit_action)
    
        help_menu = self.menuBar().addMenu(tr("About"))
        help_menu.addAction(about_act)
        help_menu.addAction(about_qt_act)

    def change_mode(self, index):
        modes = (QCompleter.InlineCompletion, QCompleter.PopupCompletion, QCompleter.UnfilteredPopupCompletion)
        self._completer.setCompletionMode(modes[index])

    def model_from_file(self, file_name):
        # file = QFile(file_name)
        # if not file.open(QFile.ReadOnly):
        #     return QStringListModel(self._completer)
        QApplication.instance().setOverrideCursor(QCursor(Qt.WaitCursor))
        model = QStandardItemModel(self._completer)
        parents = [model.invisibleRootItem()]

        with open(file_name) as file:
            pat = re.compile("^\\s+")
            for line in file:
                if not line:
                    continue
                trimmed_line = line.strip()
                if not trimmed_line:
                    continue
                match = pat.match(line)
                if not match:
                    level = 0
                else:
                    length = match.end() - match.start()
                    if line.startswith("\t"):
                        level = length
                    else:
                        level = length//4

                while len(parents) < level + 2:
                    parents.append(None)

                item = QStandardItem()
                item.setText(trimmed_line)
                parents[level].appendRow(item)
                parents[level+1] = item
        QApplication.instance().restoreOverrideCursor()
        return model

    @pyqtSlot(QModelIndex)
    def highlight(self, index):
        proxy = self._completer.completionModel()
        source_index = proxy.mapToSource(index)
        self._tree_view.selectionModel().select(source_index,
                                                QItemSelectionModel.ClearAndSelect | QItemSelectionModel.Rows)
        self._tree_view.scrollTo(source_index)

    def about(self):
        QMessageBox.about(self,
                          tr("About"),
                          tr("This example demonstrates how to use a QCompleter with a custom tree datamodel."))

    def change_case(self, cs):
        self._completer.setCaseSensitivity(Qt.CaseSensitive if cs else Qt.CaseInsensitive)

    def update_contents_label(self, sep):
        self._contents_label.setText(
            "Type path from datamodel above with items at each level separated by a '%s'" % sep)


app = QApplication(sys.argv)
w = MainWindow()
w.show()
app.exec_()
sys.exit()
