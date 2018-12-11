#
# Copyright (c) 2014 Jan de Visser (jan@sweattrails.com)
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


import collections
import datetime
import enum
import traceback

from PyQt5.QtCore import QMargins
from PyQt5.QtCore import QRegExp
from PyQt5.QtCore import Qt
from PyQt5.QtCore import pyqtSignal
from PyQt5.QtGui import QDoubleValidator
from PyQt5.QtGui import QIntValidator
from PyQt5.QtGui import QPixmap
from PyQt5.QtGui import QRegExpValidator
from PyQt5.QtWidgets import QButtonGroup
from PyQt5.QtWidgets import QCheckBox
from PyQt5.QtWidgets import QComboBox
from PyQt5.QtWidgets import QDateEdit
from PyQt5.QtWidgets import QDateTimeEdit
from PyQt5.QtWidgets import QGridLayout
from PyQt5.QtWidgets import QGroupBox
from PyQt5.QtWidgets import QHBoxLayout
from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QLineEdit
from PyQt5.QtWidgets import QMessageBox
from PyQt5.QtWidgets import QPushButton
from PyQt5.QtWidgets import QRadioButton
from PyQt5.QtWidgets import QTabWidget
from PyQt5.QtWidgets import QTimeEdit
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe
import gripe.conversions
import gripe.db
import grumble.model
import grumble.property
from grumble.property import TextProperty
from grumble.property import StringProperty
from grumble.property import LinkProperty
from grumble.property import PasswordProperty
from grumble.property import TimeDeltaProperty
from grumble.property import IntegerProperty
from grumble.property import FloatProperty
from grumble.property import DateProperty
from grumble.property import DateTimeProperty
from grumble.property import TimeProperty
from grumble.property import BooleanProperty

logger = gripe.get_logger(__name__)


class DisplayConverter:
    _delegate = None
    _suffix = None

    def __init__(self, bridge):
        self._delegate = bridge.converter if hasattr(bridge, "converter") else None
        self._property = bridge.property
        self._suffix = bridge.config.get("suffix")
        self._label = bridge.config.get("label", bridge.config.get("verbose_name"))
        if not self._label:
            self._label = self._property.verbose_name

    def label(self, instance):
        return self._delegate.label(instance) \
            if self._delegate \
            else self._label

    def suffix(self, instance):
        return self._delegate.suffix(instance) \
            if self._delegate \
            else self._suffix

    def to_display(self, value, instance):
        return self._delegate.to_display(value, instance) \
            if self._delegate \
            else value

    def from_display(self, displayvalue, instance):
        return self._delegate.from_display(displayvalue, instance) \
            if self._delegate \
            else displayvalue


class WidgetBridgeFactory(type):
    _widget_bridge_types = {}

    def __new__(mcs, name, bases, dct, **kwargs):
        ret = type.__new__(mcs, name, bases, dct)
        if "grumbletype" in kwargs:
            mcs.register_widget_bridge_type(ret, kwargs["grumbletype"])
        if "grumbletypes" in kwargs:
            mcs.register_widget_bridge_type(ret, *kwargs["grumbletypes"])
        if "qttype" in kwargs:
            ret._qt_type = kwargs["qttype"]
        if "pytype" in kwargs:
            ret._py_type = kwargs["pytype"]
        return ret

    @classmethod
    def register_widget_bridge_type(mcs, bridge_type, *grumble_types):
        for t in grumble_types:
            mcs._widget_bridge_types[t] = bridge_type

    @classmethod
    def get_widget_bridge_type(mcs, prop):
        return mcs._widget_bridge_types.get(prop.__class__)

    @classmethod
    def get(mcs, parent, kind, prop_name, **kwargs):
        prop = getattr(kind, prop_name)

        # Allow for custom bridges. Note that if you configure
        # a custom bridge, you have to deal with read-onliness and
        # multiple-choiciness yourself.
        bridge = kwargs.get("bridge", prop.config.get("bridge"))
        if bridge:
            if not isinstance(bridge, WidgetBridgeFactory):
                bridge = gripe.resolve(bridge)
            return bridge(parent, kind, prop_name, **kwargs)

        if "readonly" in kwargs or \
                prop.is_key or \
                prop.readonly:
            return Label(parent, kind, prop_name, **kwargs)
        if prop.config.get("choices") or kwargs.get("choices"):
            if kwargs.get("style", "combo").lower() == "combo":
                return ComboBox(parent, kind, prop_name, **kwargs)
            elif kwargs["style"].lower() == "radio":
                return RadioButtons(parent, kind, prop_name, **kwargs)
            # else we fall down to default processing...
        bridge = mcs.get_widget_bridge_type(prop)
        assert bridge, "I'm not ready to handle properties of type '%s'" % type(prop)
        return bridge(parent, kind, prop_name, **kwargs)


class WidgetBridge(object, metaclass=WidgetBridgeFactory):
    def __init__(self, parent, kind, path, **kwargs):
        self.parent = parent
        self.name = path
        prop_name = path.split(".")[-1]
        self.property = getattr(kind, prop_name)
        self.config = dict(self.property.config)
        self.config.update(kwargs)
        self.converter = DisplayConverter(self)
        if "displayconverter" in self.config and callable(self.config["displayconverter"]):
            self.converter = self.config["displayconverter"](self)
        cls = self.__class__
        if hasattr(cls, "getDisplayConverter") and callable(cls.getDisplayConverter):
            self.converter = cls.getDisplayConverter(self)
        self.choices = self.config.get("choices")
        self.hasLabel = self.config.get("has_label", True)
        self.assigned = None
        self.container = None
        self.suffix = None
        self.label = None
        self.widget = self.create()
        if not self.container:
            self.container = self.widget

    def get_widget_type(self):
        print("get_widget_type", type(self), self._qt_type)
        return self._qt_type

    def create(self):
        self.widget = self.create_widget()
        if hasattr(self, "customize") and callable(self.customize):
            self.customize(self.widget)
        if self.converter.suffix(None):
            self.container = QWidget(self.parent)
            self.suffix = QLabel("", self.parent)
            hbox = QHBoxLayout(self.container)
            hbox.addWidget(self.widget)
            hbox.addWidget(self.suffix)
            hbox.addStretch(1)
            hbox.setContentsMargins(QMargins(0, 0, 0, 0))
        if self.converter.label(None) and self.hasLabel:
            self.label = QLabel(self.parent)
            self.label.setBuddy(self.widget)
        return self.widget

    def create_widget(self):
        return self.get_widget_type()(parent=self.parent)

    def set_value(self, instance):
        if self.label:
            self.label.setText(str(self.converter.label(instance)) + ":")
        if self.suffix:
            self.suffix.setText(str(self.converter.suffix(instance)))
        value = getattr(instance, self.property.name)
        display_value = self.converter.to_display(value, instance)
        self.assigned = display_value
        self.apply(display_value)

    def apply(self, value):
        self.widget.setText(str(value))

    def get_value(self, instance):
        display_value = self.retrieve()
        value = self.converter.from_display(display_value, instance)
        return value

    def retrieve(self):
        return self._pytype(self.widget.text())

    def is_modified(self):
        return self.assigned != self.retrieve()


class Label(WidgetBridge, qttype=QLabel):
    def __init__(self, parent, kind, path, **kwargs):
        super(Label, self).__init__(parent, kind, path, **kwargs)
        assert self.converter

    @classmethod
    def get_display_converter(cls, bridge_instance):
        bridge = WidgetBridgeFactory.get_widget_bridge_type(bridge_instance.property)
        return bridge.get_display_converter(bridge_instance) \
            if bridge and gripe.hascallable(bridge, "get_display_converter") \
            else bridge_instance.converter

    def apply(self, value):
        fmt = self.config.get("format")
        if fmt:
            if callable(fmt):
                value = fmt(value)
            else:
                fmt = "{:" + str(fmt) + "}"
                value = fmt.format(value) if value is not None else ''
        self.widget.setText(str(value))

    def retrieve(self):
        pass

    def is_modified(self):
        return False


class Image(Label):
    def customize(self, widget):
        del widget
        self.height = int(self.config.get("height", 0))
        self.width = int(self.config.get("width", 0))
        if self.height and not self.width:
            self.width = self.height
        if self.width and not self.height:
            self.height = self.width

    def apply(self, value):
        if isinstance(value, str):
            value = QPixmap(value)
        assert isinstance(value, QPixmap), "Image bridge must be assigned a pixmap"
        if self.width and self.height:
            value = value.scaled(self.width, self.height)
        self.widget.setPixmap(value)


class TimeDeltaConverter(DisplayConverter):
    def __init__(self, bridge):
        super(TimeDeltaConverter, self).__init__(bridge)

    def to_display(self, value, instance):
        return gripe.conversions.timedelta_to_string(value)


class TimeDeltaLabel(Label, grumbletype=TimeDeltaProperty):
    @classmethod
    def get_display_converter(cls, bridge):
        return TimeDeltaConverter(bridge)


class LineEdit(WidgetBridge, qttype=QLineEdit, pytype=str, grumbletypes=[TextProperty, StringProperty, LinkProperty]):
    def customize(self, widget):
        regexp = self.config.get("regexp")
        validator = None
        if regexp:
            validator = QRegExpValidator(QRegExp(regexp), self.parent)
            if "casesensitive" in self.config:
                cs = bool(self.config.get("casesensitive"))
                validator.setCaseSensitivity(
                    Qt.CaseSensitive if cs else Qt.CaseInsensitive)
        maxlength = int(self.config.get("maxlength", 0))
        if maxlength > 0:
            widget.setMaxLength(maxlength)
            fm = widget.fontMetrics()
            widget.setMaximumWidth(maxlength * fm.maxWidth() + 11)
        if gripe.hascallable(self, "create_validator"):
            validator = self.create_validator()
        if validator:
            widget.setValidator(validator)


class PasswordEdit(LineEdit, grumbletype=PasswordProperty):
    def customize(self, widget):
        super(PasswordEdit, self).customize(widget)
        widget.setEchoMode(QLineEdit.Password)


class IntEdit(LineEdit, grumbletype=IntegerProperty, pytype=int):
    def customize(self, widget):
        super(IntEdit, self).customize(widget)
        fm = widget.fontMetrics()
        sz = None
        if "min" in self.config:
            sz = fm.width(str(self.config["min"]))
        if "max" in self.config:
            sz = max(sz, fm.width(str(self.config["max"])))
        if not sz:
            sz = fm.width("-50000")
        widget.setMaximumWidth(sz + 20)

    def create_validator(self):
        validator = QIntValidator(self.parent)
        if "min" in self.config:
            validator.setBottom(int(self.config["min"]))
        if "max" in self.config:
            validator.setTop(int(self.config["max"]))
        return validator


class FloatEdit(LineEdit, grumbletype=FloatProperty, pytype=float):
    def customize(self, widget):
        super(FloatEdit, self).customize(widget)
        fm = widget.fontMetrics()
        sz = None
        if "decimals" in self.config:
            decimals = int(self.config["decimals"])
        else:
            decimals = 4
        decwidth = fm.width(".") + decimals * fm.width("0")
        if "min" in self.config:
            sz = fm.width(str(self.config["min"]))
        if "max" in self.config:
            sz = max(sz, fm.width(str(self.config["max"])))
        if not sz:
            sz = fm.width("1000000")
        widget.setMaximumWidth(sz + decwidth + 20)

    def create_validator(self):
        validator = QDoubleValidator(self.parent)
        if "min" in self.config:
            validator.setBottom(int(self.config["min"]))
        if "max" in self.config:
            validator.setTop(int(self.config["max"]))
        if "decimals" in self.config:
            validator.setDecimals(int(self.config["decimals"]))
        return validator


class DateEdit(WidgetBridge, grumbletype=DateProperty, qttype=QDateEdit, pytype=datetime.date):
    def customize(self, widget):
        widget.setDisplayFormat("MMMM d, yyyy")
        widget.setCalendarPopup(True)
        fm = widget.fontMetrics()
        widget.setMaximumWidth(fm.width("September 29, 2000") + 31)  # FIXME
        self.assigned = None

    def apply(self, value):
        self.widget.setDate(value if value else datetime.date.today())

    def retrieve(self):
        return self.widget.date().toPyDate()


class DateTimeEdit(WidgetBridge, grumbletype=DateTimeProperty, qttype=QDateTimeEdit, pytype=datetime.datetime):
    def customize(self, widget):
        widget.setDisplayFormat("MMMM d, yyyy h:mm:ss ap")
        widget.setCalendarPopup(True)
        fm = widget.fontMetrics()
        widget.setMaximumWidth(fm.width("September 29, 2000 12:00:00 pm") + 31)  # FIXME
        self.assigned = None

    def apply(self, value):
        self.widget.setDateTime(value)

    def retrieve(self):
        return self.widget.dateTime().toPyDateTime()


class TimeEdit(WidgetBridge, grumbletype=TimeProperty, qttype=QTimeEdit, pytype=datetime.time):
    def customize(self, widget):
        widget.setDisplayFormat("h:mm:ss ap")
        fm = widget.fontMetrics()
        widget.setMaximumWidth(fm.width("12:00:00 pm") + 31)  # FIXME
        self.assigned = None

    def apply(self, value):
        self.widget.setTime(value)

    def retrieve(self):
        return self.widget.time().toPython()


class CheckBox(WidgetBridge, grumbletype=BooleanProperty, qttype=QCheckBox, pytype=bool):
    def customize(self, widget):
        widget.setText(self.label)
        self.hasLabel = False

    def apply(self, value):
        self.widget.setChecked(value)

    def retrieve(self):
        return self.widget.isChecked()


class Choices:
    def _initialize_choices(self):
        if not hasattr(self, "_choices"):
            self._choices = collections.OrderedDict()
            if hasattr(self, "choices") and self.choices:
                if hasattr(self, "required") and not self.required:
                    self._choices[None] = ""
                for c in self.choices:
                    # self.choices can be a listy thing or a dicty thing
                    # we try to access it as a dicty thing first, and if
                    # that bombs we assume it's a listy thing.
                    if isinstance(c, enum.Enum):
                        (_, _, c) = str(c).rpartition('.')
                        self._choices[c] = c
                    else:
                        try:
                            self._choices[c] = self.choices[c]
                        except (TypeError, KeyError):
                            self._choices[c] = c

    def choicesdict(self):
        self._initialize_choices()
        return self._choices

    def index(self, value):
        self._initialize_choices()
        count = 0
        for c in self._choices:
            if c == value:
                return count
            count += 1
        return -1

    def at(self, index):
        self._initialize_choices()
        count = 0
        for c in self._choices:
            if count == index:
                return c
            count += 1
        return None


class ComboBox(WidgetBridge, Choices, qttype=QComboBox):
    def __init__(self, parent, kind, propname, **kwargs):
        super(ComboBox, self).__init__(parent, kind, propname, **kwargs)

    def customize(self, widget):
        assert self.choices, "ComboBox: Cannot build widget bridge without choices"
        self.required = "required" in self.config
        for (key, text) in self.choicesdict().items():
            widget.addItem(text, key)

    def apply(self, value):
        self.assigned = value
        self.widget.setCurrentIndex(self.index(value))

    def retrieve(self):
        return self.at(self.widget.currentIndex())


class RadioButtons(WidgetBridge, Choices, qttype=QGroupBox):
    def customize(self, widget):
        assert self.choices, "RadioButtons: Cannot build widget bridge without choices"
        # We give the GroupBox a container so we can add stretch at the end.
        self.container = QWidget(self.parent)
        hbox = QHBoxLayout(self.container)
        self.buttongroup = QButtonGroup(self.parent)
        if "direction" in self.config and \
                self.config["direction"].lower() == "vertical":
            box = QVBoxLayout()
        else:
            box = QHBoxLayout()
        ix = 1
        for text in self.choicesdict().values():
            rb = QRadioButton(text, self.parent)
            box.addWidget(rb)
            self.buttongroup.addButton(rb, ix)
            ix += 1
        widget.setLayout(box)
        hbox.addWidget(widget)
        hbox.addStretch(1)
        hbox.setContentsMargins(QMargins(0, 0, 0, 0))

    def apply(self, value):
        for b in self.buttongroup.buttons():
            b.setChecked(False)
        b = self.buttongroup.button(self.index(value) + 1)
        if b:
            b.setChecked(True)

    def retrieve(self):
        ix = self.buttongroup.checkedId()
        return self.at(ix - 1) if ix > 0 else None


class PropertyFormLayout(QGridLayout):
    def __init__(self, parent=None):
        super(PropertyFormLayout, self).__init__(parent)
        self._properties = {}
        self.sublayouts = []

    def addProperty(self, parent, kind, path, row, col, *args, **kwargs):
        pnames = path.split(".")
        pname = pnames[-1]
        bridge = WidgetBridgeFactory.get(parent, kind, pname, **kwargs)
        self._properties[path] = bridge
        rowspan = int(kwargs.get("rowspan", 1))
        if bridge.label:
            labelspan = int(kwargs.get("labelspan", 1))
            self.addWidget(bridge.label, row, col,
                           rowspan, labelspan)
            col += labelspan
        colspan = int(kwargs.get("colspan", 1))
        self.addWidget(bridge.container, row, col, rowspan, colspan)

    def addSubLayout(self, layout):
        self.addSubLayouts(layout)

    def addSubLayouts(self, *layouts):
        for layout in layouts:
            self.sublayouts.append(layout)

    def addLayout(self, layout, *args):
        if isinstance(layout, PropertyFormLayout):
            self.sublayouts.append(layout)
        super(PropertyFormLayout, self).addLayout(layout, *args)

    def _setValues(self, instance):
        for (p, bridge) in self._properties.items():
            path = p.split(".")
            i = instance
            for n in path[:-1]:
                i = getattr(i, n)
            # logger.debug("Set bridge widget value: %s(%s/%s), %s", bridge.name, bridge.__class__.__name__,
            #         bridge.widget.__class__.__name__, i)
            bridge.set_value(i)
        for s in self.sublayouts:
            s._setValues(instance)

    def apply(self, instance):
        with gripe.db.Tx.begin():
            self._setValues(instance)

    def _getValues(self, instance):
        instances = set()
        for (prop, bridge) in filter(lambda p, b: b.is_modified(), self._properties.items()):
            path = prop.split(".")
            i = instance
            for n in path[:-1]:
                i = getattr(i, n)
            v = bridge.get_value(i)
            setattr(i, path[-1], v)
            instances.add(i)
            bridge.set_value(i)
        for s in self.sublayouts:
            instances |= s._getValues(instance)
        return instances

    def retrieve(self, instance):
        with gripe.db.Tx.begin():
            instances = self._getValues(instance)
            for i in instances:
                i.put()


class FormPage(QWidget):
    statusMessage = pyqtSignal(str)

    def __init__(self, parent):
        super(FormPage, self).__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        formframe = QGroupBox(self)
        self.vbox = QVBoxLayout(formframe)
        self.form = PropertyFormLayout()
        self.vbox.addLayout(self.form)
        self.vbox.addStretch(1)
        self._has_stretch = True
        layout.addWidget(formframe)

    def _removeStretch(self):
        if self._has_stretch:
            self.vbox.removeItem(self.vbox.itemAt(self.vbox.count() - 1))
            self._has_stretch = False

    def addStretch(self):
        if not self._has_stretch:
            self.vbox.addStretch(1)
            self._has_stretch = True

    def addProperty(self, kind, path, row, col, *args, **kwargs):
        self.form.addProperty(self, kind, path, row, col, *args, **kwargs)

    def addWidget(self, widget, *args):
        self._removeStretch()
        self.form.addWidget(widget, *args)

    def addLayout(self, sublayout, *args):
        self._removeStretch()
        self.form.addLayout(sublayout, *args)

    def status_message(self, msg, *args):
        self.statusMessage.emit(msg.format(*args))


class FormButtons(object):
    NoButtons = 0
    SaveButton = 1
    ResetButton = 2
    EditButtons = 3
    DeleteButton = 4
    AllButtons = 7


class FormWidget(FormPage):
    instanceAssigned = pyqtSignal(str)
    instanceDeleted = pyqtSignal(str)
    instanceSaved = pyqtSignal(str)
    exception = pyqtSignal(str)

    def __init__(self, parent=None, buttons=FormButtons.EditButtons):
        super(FormWidget, self).__init__(parent)
        self.buildButtonBox(buttons)
        self.tabs = None
        self._tabs = {}

    def buildButtonBox(self, buttons):
        buttonWidget = QGroupBox()
        self.buttonbox = QHBoxLayout(buttonWidget)
        if buttons & FormButtons.DeleteButton:
            self.deletebutton = QPushButton("Delete", self)
            self.deletebutton.clicked.connect(self.deleteInstance)
            self.buttonbox.addWidget(self.deletebutton)
        self.buttonbox.addStretch(1)
        if buttons & FormButtons.ResetButton:
            self.resetbutton = QPushButton("Reset", self)
            self.resetbutton.clicked.connect(self.setInstance)
            self.buttonbox.addWidget(self.resetbutton)
        if buttons & FormButtons.SaveButton:
            self.savebutton = QPushButton("Save", self)
            self.savebutton.clicked.connect(self.save)
            self.buttonbox.addWidget(self.savebutton)
        self.layout().addWidget(buttonWidget)

    def addWidgetToButtonBox(self, widget, *args):
        self.buttonbox.insertWidget(0, widget, *args)

    def addTab(self, widget, title):
        if self.tabs is None:
            self.tabs = QTabWidget(self)
            self.tabs.currentChanged[int].connect(self.tabChanged)

            # Remove stretch at the bottom:
            self._removeStretch()
            self.vbox.addWidget(self.tabs, 1)
        if isinstance(widget, FormPage):
            self.form.addSubLayout(widget.form)
        self.tabs.addTab(widget, title)
        self._tabs[title] = widget
        return widget

    def count(self):
        return self.tabs and self.tabs.count()

    def setTab(self, tab):
        if self.tabs and tab <= self.tabs.count():
            self.tabs.setCurrentIndex(tab)

    def tabChanged(self, ix):
        w = self.tabs.currentWidget()
        if hasattr(w, "selected"):
            w.selected()

    def save(self):
        try:
            self.form.retrieve(self.instance())
            if hasattr(self, "retrieve") and callable(self.retrieve):
                self.retrieve(self.instance())
            self.instanceSaved.emit(str(self.instance.key()))
            self.statusMessage.emit("Saved")
        except Exception:
            self.exception.emit("Save failed...")
        self.setInstance()

    def instance(self):
        return self._instance

    def setInstance(self, instance=None):
        if instance:
            self._instance = instance
        self.form.apply(self.instance())
        if hasattr(self, "assign") and callable(self.assign):
            self.assign(self.instance())
        self.instanceAssigned.emit(str(self.instance().key()))

    def confirmDelete(self):
        return QMessageBox.warning(self, "Are you sure?",
                                   "Are you sure you want to delete this?",
                                   QMessageBox.Cancel | QMessageBox.Ok,
                                   QMessageBox.Cancel) == QMessageBox.Ok

    def onDelete(self):
        try:
            with gripe.db.Tx.begin():
                key = str(self.instance().key())
                if grumble.model.delete(self.instance()):
                    self.instanceDeleted.emit(key)
        except Exception:
            traceback.print_exc()
            self.exception.emit("Delete failed...")

    def deleteInstance(self):
        if self.instance() and self.confirmDelete():
            self.onDelete()
