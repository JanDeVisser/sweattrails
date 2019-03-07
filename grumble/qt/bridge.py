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
import sys
import traceback

from PyQt5.QtCore import QMargins
from PyQt5.QtCore import QModelIndex
from PyQt5.QtCore import QObject
from PyQt5.QtCore import QRegExp
from PyQt5.QtCore import Qt
from PyQt5.QtCore import pyqtSignal
from PyQt5.QtCore import pyqtSlot

from PyQt5.QtGui import QDoubleValidator, QStandardItemModel, QValidator
from PyQt5.QtGui import QIntValidator
from PyQt5.QtGui import QPixmap
from PyQt5.QtGui import QRegExpValidator

from PyQt5.QtWidgets import QApplication
from PyQt5.QtWidgets import QButtonGroup
from PyQt5.QtWidgets import QCheckBox
from PyQt5.QtWidgets import QComboBox
from PyQt5.QtWidgets import QCompleter
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
import grumble.qt.model
from grumble.reference import ReferenceProperty

logger = gripe.get_logger(__name__)


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
    def get_widget_bridge_type(mcs, prop, prop_name=None):
        if prop:
            classes = [prop.__class__]
            while classes:
                cls = classes.pop(0)
                if cls in mcs._widget_bridge_types:
                    return mcs._widget_bridge_types[cls]
                else:
                    classes.extend(cls.__bases__)
            return None
        elif prop_name in "^$":
            return References
        else:
            return LineEdit

    @classmethod
    def get(mcs, parent, kind, prop_name, **kwargs):
        prop = getattr(kind, prop_name) \
            if prop_name and prop_name not in "^$" and kind and hasattr(kind, prop_name) \
            else None
        config = dict(prop.config if prop and hasattr(prop, "config") else {})
        config.update(**kwargs)

        # Allow for custom bridges. Note that if you configure
        # a custom bridge, you have to deal with read-onliness and
        # multiple-choiciness yourself.
        bridge = config.get("bridge")
        if bridge:
            if not isinstance(bridge, WidgetBridgeFactory):
                bridge = gripe.resolve(bridge)
            return bridge(parent, kind, prop_name, **kwargs)

        if "choices" in config:
            if config.get("style", "combo").lower() == "combo":
                return ComboBox(parent, kind, prop_name, **kwargs)
            elif config["style"].lower() == "radio":
                return RadioButtons(parent, kind, prop_name, **kwargs)
            # else we fall down to default processing...
        bridge = mcs.get_widget_bridge_type(prop, prop_name)
        assert bridge, "I'm not ready to handle properties of type '%s'" % type(prop)
        return bridge(parent, kind, prop_name, **kwargs)


class WidgetBridge(metaclass=WidgetBridgeFactory):
    def __init__(self, parent, kind, path, **kwargs):
        self.parent = parent
        self.name = path
        prop_name = path.split(".")[-1]
        self.kind = kind
        self.property: grumble.property.ModelProperty = getattr(kind, prop_name) \
            if kind and prop_name not in "^$" and hasattr(kind, prop_name) \
            else kwargs.get("property")
        self.config = dict(self.property.config) if self.property else {}
        self.config.update(kwargs)
        self._readonly_new = "readonly" in kwargs or (self.property and self.property.readonly)
        self._readonly = self._readonly_new or \
                         (self.property and self.property.is_key) or \
                         (self.property is None and prop_name in "^$") or \
                         False
        self._ro_flag = self._readonly
        self.choices = self.config.get("choices")
        self.hasLabel = self.config.get("has_label", True)
        self.assigned = None
        self.widget = None
        self._suffix = None
        self._label = None
        self._ro_text = None
        self._ro_suffix = None
        self._ro = None

    def readonly(self, readonly=None):
        if readonly is not None:
            self.widget.setVisible(not readonly)
            self._ro.setVisible(readonly)
            self._ro_flag = readonly
        return self._ro_flag

    def get_label(self, instance):
        if hasattr(self, "label"):
            return self.label(instance)
        elif "label" in self.config:
            return self.config["label"](instance) if callable(self.config["label"]) else self.config["label"]
        elif self.property:
            return self.property.verbose_name
        else:
            return None

    def get_suffix(self, instance):
        if hasattr(self, "suffix"):
            return self.suffix(instance)
        elif "suffix" in self.config:
            return self.config["suffix"](instance) if callable(self.config["suffix"]) else self.config["suffix"]
        else:
            return None

    def to_display(self, value, instance=None):
        if hasattr(self, "display"):
            return self.display(value, instance)
        elif "display" in self.config:
            return self.config["display"](value, instance) \
                if callable(self.config["display"]) else self.config["display"]
        elif self.property:
            return self.property.to_display(value, instance)
        elif "format" in self.config:
            return ("{:" + self.config["format"] + "}").format(value) if value is not None else ''
        elif isinstance(value, (grumble.Key, grumble.Model)):
            return value().label()
        elif value is None:
            return ''
        else:
            return str(value)

    def from_display(self, display_value, instance):
        if hasattr(self, "parse"):
            return self.parse(display_value, instance)
        elif "parse" in self.config:
            return self.config["parse"](display_value, instance) \
                if callable(self.config["parse"]) else self.config["parse"]
        else:
            return display_value

    def empty(self):
        if hasattr(self, "default"):
            e = self.default() if callable(self.default) else self.default
        elif "empty" in self.config:
            e = self.config["empty"]() if callable(self.config["empty"]) else self.config["empty"]
        else:
            e = None
        return e

    def get_widget_type(self):
        return self._qt_type

    def create(self):
        self.widget = None
        self._label = None
        self._suffix = None
        self._ro_text = None
        self._ro_suffix = None
        self._ro = None
        widget = self.create_widget()
        if hasattr(self, "customize") and callable(self.customize):
            widget = self.customize(widget)
        if "customize" in self.config:
            widget = self.config["customize"](self, widget)
        if "suffix" in self.config or hasattr(self, "suffix"):
            container = QWidget()
            self._suffix = QLabel("")
            hbox = QHBoxLayout(container)
            hbox.addWidget(widget)
            hbox.addWidget(self._suffix)
            hbox.addStretch(1)
            hbox.setContentsMargins(QMargins(0, 0, 0, 0))
            self.widget = container
        else:
            self.widget = widget
        if self.readonly():
            self.widget.hide()
        if ("label" in self.config or hasattr(self, "label") or self.property) and self.hasLabel:
            self._label = QLabel()
            self._label.setBuddy(self.widget)
        self._ro_text = QLabel()
        if self._suffix:
            container = QWidget()
            self._ro_suffix = QLabel("")
            hbox = QHBoxLayout(container)
            hbox.addWidget(self._ro_text)
            hbox.addWidget(self._suffix)
            hbox.addStretch(1)
            hbox.setContentsMargins(QMargins(0, 0, 0, 0))
            self._ro = container
        else:
            self._ro = self._ro_text
        if not self.readonly():
            self._ro.hide()
        return self.widget

    def create_widget(self):
        return self.get_widget_type()()

    def set_value(self, instance):
        self.readonly(self._readonly)
        self.set_label(instance)
        suf = str(self.get_suffix(instance))
        if self._suffix:
            self._suffix.setText(suf)
        if self._ro_suffix:
            self._ro_suffix.setText(suf)
        value = self.config.get("value")
        if value:
            value = value(instance) if callable(value) else value
        elif self.property:
            value = getattr(instance, self.property.name) if instance else None
        elif self.name == "^":
            value = instance.parent_key()
        elif self.name == "$":
            value = instance.root()
        else:
            raise NotImplementedError("Could not determine value of widget bridge property " + self.name)
        self._apply(value)

    def clear(self):
        self.set_label(None)
        self.readonly(self._readonly_new)
        self._apply(self.empty())

    def set_label(self, instance):
        if self._label:
            self._label.setText(str(self.get_label(instance)) + ":")

    def _apply(self, internal):
        display = self.to_display(internal)
        self.assigned = display
        self._ro_text.setText(display)
        self.apply(display, internal)

    def apply(self, value, internal):
        self.widget.setText(str(value))

    def get_value(self, instance):
        display_value = self.retrieve()
        value = self.from_display(display_value, instance)
        return value

    def retrieve(self):
        return self._py_type(self.widget.text())

    def is_modified(self):
        return self.assigned != self.retrieve()


class Label(WidgetBridge, qttype=QLabel):
    def __init__(self, parent, kind, path, **kwargs):
        super(Label, self).__init__(parent, kind, path, **kwargs)
        assert self.converter

    def retrieve(self):
        pass

    def is_modified(self):
        return False


class Image(Label, qttype=QWidget):
    def customize(self, widget):
        self.height = int(self.config.get("height", 0))
        self.width = int(self.config.get("width", 0))
        if self.height and not self.width:
            self.width = self.height
        if self.width and not self.height:
            self.height = self.width
        return widget

    def apply(self, value, internal):
        if isinstance(value, str):
            value = QPixmap(value)
        assert isinstance(value, QPixmap), "Image bridge must be assigned a pixmap"
        if self.width and self.height:
            value = value.scaled(self.width, self.height)
        self.widget.setPixmap(value)


class TimeDeltaLabel(Label, grumbletype=TimeDeltaProperty):
    def __init__(self, *args, **kwargs):
        super(TimeDeltaLabel, self).__init__(*args, **kwargs)

    def display(self, value, instance):
        return gripe.conversions.timedelta_to_string(value)


class LineEdit(WidgetBridge, qttype=QLineEdit, pytype=str, grumbletypes=[TextProperty, StringProperty, LinkProperty]):
    def customize(self, widget):
        regexp = self.config.get("regexp")
        if regexp:
            validator = QRegExpValidator(QRegExp(regexp))
            if "casesensitive" in self.config:
                cs = bool(self.config.get("casesensitive"))
                validator.setCaseSensitivity(
                    Qt.CaseSensitive if cs else Qt.CaseInsensitive)
            widget.setValidator(validator)
        maxlength = int(self.config.get("maxlength", 0))
        if maxlength > 0:
            widget.setMaxLength(maxlength)
            fm = widget.fontMetrics()
            widget.setMaximumWidth(maxlength * fm.maxWidth() + 11)
        validator = gripe.call_if_exists(self, "create_validator", None)
        if validator:
            widget.setValidator(validator)
        return widget


class PasswordEdit(LineEdit, grumbletype=PasswordProperty):
    def customize(self, widget):
        widget = super(PasswordEdit, self).customize(widget)
        widget.setEchoMode(QLineEdit.Password)
        return widget


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
        return widget

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
        return widget

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
        return widget

    def apply(self, value, internal):
        self.widget.setDate(internal if internal else datetime.date.today())

    def retrieve(self):
        return self.widget.date().toPyDate()

    def display(self, value: datetime.date, instance):
        return value.strftime("%x") if value else ''


class DateTimeEdit(WidgetBridge, grumbletype=DateTimeProperty, qttype=QDateTimeEdit, pytype=datetime.datetime):
    def customize(self, widget):
        widget.setDisplayFormat("MMMM d, yyyy h:mm:ss ap")
        widget.setCalendarPopup(True)
        fm = widget.fontMetrics()
        widget.setMaximumWidth(fm.width("September 29, 2000 12:00:00 pm") + 31)  # FIXME
        self.assigned = None
        return widget

    def apply(self, value, internal):
        self.widget.setDateTime(internal if internal else datetime.datetime.now())

    def retrieve(self):
        return self.widget.dateTime().toPyDateTime()

    def display(self, value: datetime.datetime, instance):
        return value.strftime("%c") if value else ''


class TimeEdit(WidgetBridge, grumbletype=TimeProperty, qttype=QTimeEdit, pytype=datetime.time):
    def customize(self, widget):
        widget.setDisplayFormat("h:mm:ss ap")
        fm = widget.fontMetrics()
        widget.setMaximumWidth(fm.width("12:00:00 pm") + 31)  # FIXME
        self.assigned = None
        return widget

    def apply(self, value, internal):
        self.widget.setTime(internal if internal else datetime.time.now())

    def retrieve(self):
        return self.widget.time().toPython()

    def display(self, value: datetime.time, instance):
        return value.strftime("%X") if value else ''


class CheckBox(WidgetBridge, grumbletype=BooleanProperty, qttype=QCheckBox, pytype=bool):
    def customize(self, widget):
        widget.setText(self.label)
        self.hasLabel = False
        return widget

    def apply(self, value, internal):
        self.widget.setChecked(internal)

    def retrieve(self):
        return self.widget.isChecked()


class Choices:
    def __init__(self, **kwargs):
        if "choices" in kwargs:
            self.choices = kwargs.get("choices")

    def _initialize_choices(self):
        if not hasattr(self, "_choices"):
            self._choices = collections.OrderedDict()
            if hasattr(self, "choices") and self.choices:
                if hasattr(self, "required") and not self.required:
                    self._choices[None] = ""
                choices = self.choices() if callable(self.choices) else self.choices
                for c in choices:
                    # choices can be a listy thing or a dicty thing
                    # we try to access it as a dicty thing first, and if
                    # that bombs we assume it's a listy thing.
                    if isinstance(c, enum.Enum):
                        (_, _, c) = str(c).rpartition('.')
                        self._choices[c] = c
                    else:
                        try:
                            self._choices[c] = choices[c]
                        except (TypeError, KeyError):
                            self._choices[c] = c

    def choicesdict(self):
        self._initialize_choices()
        return self._choices

    def index(self, value):
        self._initialize_choices()
        if "to_choice" in self.config:
            value = self.config["to_choice"](value)
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
        self.required = None

    def customize(self, widget):
        self.required = "required" in self.config
        for (key, text) in self.choicesdict().items():
            widget.addItem(text, key)
        return widget

    def apply(self, value, internal):
        self.assigned = internal
        self.widget.setCurrentIndex(self.index(internal))

    def get_value(self, instance):
        return self.widget.currentData()

    def retrieve(self):
        return self.at(self.widget.currentIndex())


class RadioButtons(WidgetBridge, Choices, qttype=QGroupBox):
    def customize(self, widget):
        assert self.choices, "RadioButtons: Cannot build widget bridge without choices"
        # We give the GroupBox a container so we can add stretch at the end.
        container = QWidget()
        hbox = QHBoxLayout(container)
        self.buttongroup = QButtonGroup(container)
        if "direction" in self.config and \
                self.config["direction"].lower() == "vertical":
            box = QVBoxLayout(container)
        else:
            box = QHBoxLayout(container)
        ix = 1
        for text in self.choicesdict().values():
            rb = QRadioButton(text, box)
            box.addWidget(rb)
            self.buttongroup.addButton(rb, ix)
            ix += 1
        widget.setLayout(box)
        hbox.addWidget(widget)
        hbox.addStretch(1)
        hbox.setContentsMargins(QMargins(0, 0, 0, 0))
        return container

    def apply(self, value, internal):
        for b in self.buttongroup.buttons():
            b.setChecked(False)
        b = self.buttongroup.button(self.index(internal) + 1)
        if b:
            b.setChecked(True)

    def retrieve(self):
        ix = self.buttongroup.checkedId()
        return self.at(ix - 1) if ix > 0 else None


class ReferenceCompleter(QCompleter):
    def __init__(self, model, parent):
        super(ReferenceCompleter, self).__init__(model, parent)

    def splitPath(self, path):
        return path.split(".")

    def pathFromIndex(self, index):
        # navigate up and accumulate data
        data_list = []
        i = index
        while i.isValid():
            data_list.insert(0, str(self.model().data(i, self.completionRole())))
            i = i.parent()
        return ".".join(data_list)


class ReferenceValidator(QValidator):
    def __init__(self, bridge):
        super(ReferenceValidator, self).__init__()
        self.bridge = bridge

    @staticmethod
    def ValidatorState(state):
        if state == QValidator.Invalid:
            return "Invalid"
        elif state == QValidator.Acceptable:
            return "Acceptable"
        elif state == QValidator.Intermediate:
            return "Intermediate"
        else:
            return "Unknown (%s)" % state

    def validate(self, value, pos):
        if self.bridge.key:
            ret = QValidator.Acceptable
        else:
            with gripe.db.Tx.begin():
                q = self.bridge.refclass.query(keys_only=False)
                q.add_condition("k.%s ILIKE ?" % self.bridge.column, "%s%%" % value)
                cnt = q.count()
                if cnt == 0:
                    ret = QValidator.Invalid
                else:
                    q = self.bridge.refclass.query(keys_only=False)
                    q.add_filter(self.bridge.column, "=", value)
                    ret = QValidator.Acceptable if q.count() == 1 else QValidator.Intermediate
                    if ret == QValidator.Acceptable:
                        o = q.get()
                        self.bridge.set_key(o.key())
        # logger.debug("Validating %s. key = %s. Returning %s", value, self.bridge.key, self.ValidatorState(ret))
        return ret, value, pos


class References(LineEdit, grumbletype=ReferenceProperty):
    def __init__(self, *args, **kwargs):
        super(References, self).__init__(*args, **kwargs)
        if "refclass" in kwargs:
            self.refclass = kwargs["refclass"]
        elif hasattr(self, "property") and self.property and hasattr(self.property, "reference_class"):
            self.refclass = self.property.reference_class
        else:
            self.refclass = None
        self.column = kwargs.get("display_prop")
        if not self.column and self.refclass:
            if self.refclass.label_prop:
                self.column = self.refclass.label_prop.name
            elif self.refclass.key_prop:
                self.column = self.refclass.key_prop.name
        self.model = None
        self.completer = None
        self.validator = None
        self.key = None

    def _query(self):
        if hasattr(self, "query") and callable(self.query):
            return self.query()
        elif "query" in self.config and callable(self.config["query"]):
            return self.config["query"]()
        # elif self.kind and self.refclass and self.refclass == self.kind:
        #     return self._tree()
        elif self.refclass:
            return self.refclass.query(keys_only=False)
        else:
            return {}

    def customize(self, widget):
        self.model = grumble.qt.model.ListModel(self._query(), self.column)
        self.completer = ReferenceCompleter(self.model, widget)
        self.completer.setCaseSensitivity(Qt.CaseInsensitive)
        widget.setCompleter(self.completer)
        self.completer.highlighted[QModelIndex].connect(self.highlight)
        self.validator = ReferenceValidator(self)
        widget.setValidator(self.validator)
        widget.textEdited.connect(self.text_edited)
        return widget

    def get_value(self, instance):
        ret = self.key
        self.key = None
        return ret

    def highlight(self, index):
        proxy = self.completer.completionModel()
        source_index = proxy.mapToSource(index)
        self.set_key(self.model.data(source_index, Qt.UserRole))

    def text_edited(self, text):
        self.set_key(None)

    def set_key(self, key):
        self.key = key

    def set(self, key):
        self.key = key
        self.widget.setText(self.model.text_for_key(key))

    def choices(self):
        with gripe.db.Tx.begin():
            return {i.key(): i.label() for i in self._query()}

    def _tree(self):
        instances = {}
        todo = []
        with gripe.db.Tx.begin():
            for i in self.kind().query(keys_only=False):
                k = i.key()
                pk = i.parent_key()
                d = {"key": k, "parent": pk, "code": i.keyname()}
                if pk:
                    if pk in instances:
                        pi = instances[pk]
                        if "label" in pi:
                            d["label"] = pi["label"] + "." + d["code"]
                else:
                    d["label"] = d["code"]
                instances[k] = d
                if "label" not in d:
                    todo.append(d)

        while todo:
            i = todo.pop(0)
            pk = i["parent"]
            if pk:
                pi = instances[pk]
                if "label" in pi:
                    i["label"] = pi["label"] + "." + i["code"]
                else:
                    todo.append(i)

        return {i["key"]: i["label"] for i in sorted(instances.values(), key=lambda instance: instance["label"])}


class PropertyFormLayout(QGridLayout):
    def __init__(self, parent=None, **kwargs):
        super(PropertyFormLayout, self).__init__(parent)
        self._kind = None
        self._properties = {}
        self.sublayouts = []
        self.init_instance = kwargs.get("init_instance")

    def kind(self, k=None):
        if k:
            self._kind = k
        return self._kind

    def addProperty(self, parent, kind, path, row, col, *args, **kwargs):
        pnames = path.split(".")
        pname = pnames[-1]
        kwargs["row"] = row
        kwargs["col"] = col
        bridge = WidgetBridgeFactory.get(parent, kind, pname, **kwargs)
        self._properties[path] = bridge
        self.add_widgets(bridge)

    def add_widgets(self, bridge):
        bridge.create()
        rowspan = int(bridge.config.get("rowspan", 1))
        row = bridge.config["row"]
        col = bridge.config["col"]
        if bridge._label:
            label_span = int(bridge.config.get("labelspan", 1))
            self.addWidget(bridge._label, row, col, rowspan, label_span)
            col += label_span
        colspan = int(bridge.config.get("colspan", 1))
        self.addWidget(bridge.widget, row, col, rowspan, colspan)
        self.addWidget(bridge._ro, row, col, rowspan, colspan)

    def delete_widgets(self, bridge):
        self.removeWidget(bridge.widget)
        self.removeWidget(bridge._ro)
        if bridge.label:
            self.removeWidget(bridge._label)

    def get_property_bridge(self, path):
        ret = self._properties.get(path)
        if ret is None:
            for s in self.sublayouts:
                ret = s.get_property_bridge(path)
                if ret is not None:
                    break
        return ret

    def addSubLayout(self, layout):
        self.addSubLayouts(layout)

    def addSubLayouts(self, *layouts):
        for layout in layouts:
            self.sublayouts.append(layout)

    def addLayout(self, layout, *args):
        if isinstance(layout, PropertyFormLayout):
            self.sublayouts.append(layout)
        super(PropertyFormLayout, self).addLayout(layout, *args)

    def set_values(self, instance):
        for (p, bridge) in self._properties.items():
            path = p.split(".")
            i = instance
            for n in path[:-1]:
                if n == "^":
                    i = i.parent()
                elif n == "$":
                    i = i.root()
                else:
                    i = getattr(i, n)
            # logger.debug("Set bridge widget value: %s(%s/%s), %s", bridge.name, bridge.__class__.__name__,
            #         bridge.widget.__class__.__name__, i)
            bridge.set_value(i)
        for s in self.sublayouts:
            s.set_values(instance)

    def apply(self, instance):
        with gripe.db.Tx.begin():
            self.set_values(instance)

    def set_labels(self, instance):
        for (p, bridge) in self._properties.items():
            path = p.split(".")
            i = instance
            if i:
                for n in path[:-1]:
                    i = getattr(i, n) if n != "^" else i.parent()
                    if not i:
                        break
            bridge.set_label(i)
        for s in self.sublayouts:
            s.set_labels(instance)

    def apply_labels(self, instance):
        with gripe.db.Tx.begin():
            self.set_labels(instance)

    def get_values(self, instance):
        instances = set()
        if instance is None:
            instance = gripe.call_if_exists(self.parent(), "init_instance",
                                            lambda: self.init_instance()
                                            if self.init_instance
                                            else self.kind()() if self.kind() else None)
            assert instance, "No instance created!"
        for (prop, bridge) in filter(lambda itm: itm[1].is_modified(), self._properties.items()):
            path = prop.split(".")
            i = instance
            for n in path[:-1]:
                i = getattr(i, n) if n != "^" else i.parent()
            v = bridge.get_value(i)
            p = path[-1]
            if p == "^":
                i.set_parent(v)
            else:
                setattr(i, path[-1], v)
            instances.add(i)
            bridge.set_value(i)
        for s in self.sublayouts:
            instances |= s.get_values(instance)
        return instance, instances

    def retrieve(self, instance):
        with gripe.db.Tx.begin():
            (instance, instances) = self.get_values(instance)
            for i in instances:
                i.put()
            return instance

    def _clear(self):
        for (p, bridge) in self._properties.items():
            bridge.clear()
        for s in self.sublayouts:
            s._clear()

    def clear(self):
        with gripe.db.Tx.begin():
            self._clear()


class FormPage(QWidget):
    statusMessage = pyqtSignal(str)

    def __init__(self, parent=None, **kwargs):
        super(FormPage, self).__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        form_frame = QGroupBox(self)
        self.vbox = QVBoxLayout(form_frame)
        self.form = PropertyFormLayout(None, **kwargs)
        self.vbox.addLayout(self.form)
        self.vbox.addStretch(1)
        self._has_stretch = True
        layout.addWidget(form_frame)

    def _removeStretch(self):
        if self._has_stretch:
            self.vbox.removeItem(self.vbox.itemAt(self.vbox.count() - 1))
            self._has_stretch = False

    def addStretch(self):
        if not self._has_stretch:
            self.vbox.addStretch(1)
            self._has_stretch = True

    def kind(self, k=None):
        return self.form.kind(k)

    def addProperty(self, kind, path, row, col, *args, **kwargs):
        self.form.addProperty(self, kind, path, row, col, *args, **kwargs)

    def get_property_bridge(self, path):
        return self.form.get_property_bridge(path)

    def addWidget(self, widget, *args):
        self._removeStretch()
        self.form.addWidget(widget, *args)

    def addLayout(self, sublayout, *args):
        self._removeStretch()
        self.form.addLayout(sublayout, *args)

    def status_message(self, msg, *args):
        self.statusMessage.emit(msg.format(*args))


class FormButtons:
    NoButtons = 0
    SaveButton = 1
    ResetButton = 2
    EditButtons = 3
    DeleteButton = 4
    NewButton = 8
    AllButtons = 15


class FormWidget(FormPage):
    newInstance = pyqtSignal()
    instanceAssigned = pyqtSignal(grumble.Key)
    instanceDeleted = pyqtSignal(grumble.Key)
    instanceSaved = pyqtSignal(grumble.Key)
    refresh = pyqtSignal()
    exception = pyqtSignal(str)

    def __init__(self, parent=None, buttons=FormButtons.EditButtons, **kwargs):
        super(FormWidget, self).__init__(parent, **kwargs)
        self._instance = None
        self.tabs = None
        self._tabs = {}
        self.buttonbox = None
        self.delete_button = None
        self.new_button = None
        self.save_button = None
        self.reset_button = None
        self.buildButtonBox(buttons)

    def buildButtonBox(self, buttons):
        button_widget = QGroupBox()
        self.buttonbox = QHBoxLayout(button_widget)
        if buttons & FormButtons.DeleteButton:
            self.delete_button = QPushButton("Delete", self)
            self.delete_button.clicked.connect(self.deleteInstance)
            self.buttonbox.addWidget(self.delete_button)
        if buttons & FormButtons.NewButton:
            self.new_button = QPushButton("New", self)
            self.new_button.clicked.connect(lambda: self.set_instance(None))
            self.buttonbox.addWidget(self.new_button)
        self.buttonbox.addStretch(1)
        if buttons & FormButtons.ResetButton:
            self.reset_button = QPushButton("Reset", self)
            self.reset_button.clicked.connect(self.set_instance)
            self.buttonbox.addWidget(self.reset_button)
        if buttons & FormButtons.SaveButton:
            self.save_button = QPushButton("Save", self)
            self.save_button.clicked.connect(self.save)
            self.buttonbox.addWidget(self.save_button)
        self.layout().addWidget(button_widget)

    def addWidgetToButtonBox(self, widget, *args):
        self.buttonbox.insertWidget(0, widget, *args)

    def addButton(self, label, action):
        button = QPushButton(label, self)
        button.clicked.connect(action)
        self.addWidgetToButtonBox(button)

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
        gripe.call_if_exists(self.tabs.currentWidget(), "selected", None)

    def save(self):
        try:
            self.set_instance(self.form.retrieve(self.instance()))
            if hasattr(self, "retrieve") and callable(self.retrieve):
                self.retrieve(self.instance())
            self.instanceSaved.emit(self.instance().key())
            self.refresh.emit()
            self.statusMessage.emit("Saved")
        except Exception:
            traceback.print_exc()
            self.exception.emit("Save failed...")
            self.set_instance()

    def instance(self):
        return self._instance

    def set_instance(self, instance=None):
        i = self._instance
        self._instance = instance() if isinstance(instance, (grumble.Model, grumble.Key)) else None
        if self._instance is not None and (i is None or self._instance != i):
            self.form.apply(self._instance)
            gripe.call_if_exists(self, "assign", self._instance)
            self.instanceAssigned.emit(self._instance.key())
        elif instance is None:
            self.form.clear()
            self.newInstance.emit()
        else:
            self.form.apply_labels(instance)

    def unset_instance(self):
        self._instance = None

    def confirmDelete(self):
        return QMessageBox.warning(self, "Are you sure?",
                                   "Are you sure you want to delete this?",
                                   QMessageBox.Cancel | QMessageBox.Ok,
                                   QMessageBox.Cancel) == QMessageBox.Ok

    def onDelete(self):
        try:
            with gripe.db.Tx.begin():
                key = self.instance().key()
                if grumble.model.delete(self.instance()):
                    self.instanceDeleted.emit(key)
                    self.statusMessage.emit("Deleted")
                    self.refresh.emit()
        except Exception:
            traceback.print_exc()
            self.exception.emit("Delete failed...")
        self.set_instance()

    def deleteInstance(self):
        if self.instance() and self.confirmDelete():
            self.onDelete()
