#
# Copyright (c) 2017 Jan de Visser (jan@sweattrails.com)
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

import csv
import datetime
import enum
import io
import os.path
import re
import shutil

import dateparser
from PyQt5.QtCore import pyqtSignal
from PyQt5.QtCore import QObject

import gripe.db
import gripe.json_util
import grumpy.bg

import bucks.datamodel
from bucks.datamodel.account import Account
from bucks.datamodel.account import Transaction
from bucks.datamodel.category import Category
from bucks.datamodel.contact import Contact
from bucks.datamodel.project import Project

from bucks.datamodel.dataimport import Import
from bucks.datamodel.dataimport import ImportStatus

logger = gripe.get_logger(__name__)


class PythonTypes(enum.Enum):
    date = datetime.date
    str = str
    int = int
    float = float
    bool = bool


class Field:
    def __init__(self, fldname, ix, typ, **kwargs):
        self.name = fldname
        self.index = ix
        self.type = typ if isinstance(typ, type) else PythonTypes[typ].value
        for kwarg in kwargs:
            if not hasattr(self, kwarg):
                setattr(self, kwarg, kwargs[kwarg])
        if self.type == datetime.date and not hasattr(self, "format"):
            self.format = "%m/%d/%Y"

    def convert(self, val):
        if val is None:
            return None
        if self.type == datetime.date:
            dt = dateparser.parse(val, date_formats=[self.format])
            return dt.date() if dt else None
        else:
            return self.type(val)


class Line:
    def __init__(self, fields, account, line):
        self.account = account
        self.date = None
        self.contact = None
        self.category = None
        self.project = None
        self.counter = None
        self.amount = None
        self.currency = 'CAD'
        self.foreign_amount = None
        self.type = None
        self.description = None
        for fld in fields:
            if fld is not None:
                setattr(self, fld.name, fld.convert(line[fld.name]))

    @staticmethod
    def find_or_create(cls, prop, value):
        ret = None
        if isinstance(value, cls):
            ret = value
        elif value:
            ret = cls.by(prop, value)
            if not ret:
                ret = cls(**{prop: value})
                ret.put()
        return ret

    def persist(self):
        attrs = {
            "date": self.date,
            "amt": self.amount,
            "currency": self.currency,
            "foreign_amt": self.foreign_amount,
            "description": self.description,
            "contact": self.find_or_create(Contact, "contact_name", self.contact),
            "category": self.find_or_create(Category, "cat_name", self.category),
            "project": self.find_or_create(Project, "proj_name", self.project)
        }
        if self.type == "T":
            attrs["counter"] = self.find_or_create(Account, "acc_name", self.counter)
        tx = Transaction.for_type(self.account, self.type, **attrs)
        if tx:
            tx.put()


class ImportException(Exception):
    pass


class FileAlreadyProcessedWithErrors(ImportException):
    pass


class FileEmpty(ImportException):
    pass


class FileCannotBeRead(ImportException):
    pass


class FileCannotBeProcessed(ImportException):
    pass


class Reader(grumpy.bg.Job):
    def __init__(self, account, file_name):
        super(Reader, self).__init__()
        self.account: Account = account
        self.acc_dir = os.path.join(gripe.root_dir(), "bucks", "data", self.account.acc_name)
        self.record: Import = Import.by("filename", file_name)
        if not self.record:
            self.record = Import(account=account, filename=file_name)
            self.record.put()
        self.filename: str = file_name
        self.datefmt: str = None
        self.headerline: bool = False
        self.match: str = None
        self.templates: list = []
        self.mapping: list = []
        self._parse_template()

    def _parse_template(self):
        data = gripe.json_util.JSON.file_read(os.path.join("bucks", "data", self.account.acc_name + ".json"))
        if data and "mapping" in data:
            assert isinstance(data.mapping, list)
            self.mapping = []
            ix = 0
            for fld in data.mapping:
                if fld is None:
                    fd = Field("Ignored", ix, str)
                elif isinstance(fld, str):
                    fd = Field(fld, ix, str)
                else:
                    assert isinstance(fld, dict)
                    fd = Field(fld["name"], ix, fld["type"], **fld)
                self.mapping.append(fd)
                ix += 1
        self.headerline = False
        self.match = "description"
        if data and "config" in data:
            for attr in data.config:
                setattr(self, attr, data.config[attr])
        if data and "templates" in data:
            for t in data.templates:
                self.templates.append(dict(t))

    def handle(self):
        try:
            if self.record.status == ImportStatus.Initial:
                self.record.read()
                if self.record.status == ImportStatus.Read:
                    if self.record.data:
                        try:
                            with io.StringIO(self.record.data) as s:
                                dialect = csv.Sniffer().sniff(s.read(1024))
                                s.seek(0)
                                if self.headerline:
                                    s.readline()
                                reader = csv.DictReader(s, [fld.name for fld in self.mapping], dialect=dialect)
                                with gripe.db.Tx.begin():
                                    for line in reader:
                                        l: Line = None
                                        try:
                                            l = Line(self.mapping, self.account, line)
                                            l = self.process(l)
                                        except Exception:
                                            self.record.log_error()
                                            self.record.status = ImportStatus.Partial
                                        if l and hasattr(l, "type") and l.type:
                                            l.persist()
                                    if self.record.status != ImportStatus.Partial:
                                        self.record.status = ImportStatus.Completed
                        except ImportException:
                            raise
                        except Exception:
                            self.record.log_error(FileCannotBeProcessed)
                    else:
                        raise FileEmpty()
                elif self.record.status == ImportStatus.Error:
                    raise FileCannotBeRead()
            elif self.record.status == ImportStatus.Error:
                raise FileAlreadyProcessedWithErrors()
        except ImportException as ie:
            raise
        except Exception:
            self.record.log_error(FileCannotBeProcessed)
        finally:
            self.record.put()

    def process(self, line):
        return self.match_templates(line)

    def match_templates(self, line):
        field = getattr(line, self.match)
        for tpl in self.templates:
            m = re.search(tpl["template"], field)
            if m:
                for k in (key for key in tpl if key != 'template'):
                    v = tpl[k]
                    if callable(v):
                        v = v(self.account)
                    setattr(line, k, v)
                break
        if not line.type:
            line.type = "D"
        return line


class PaypalReader(Reader):
    def __init__(self, account, file_name):
        super(PaypalReader, self).__init__(account, file_name)
        self.headerline = True
        assert self.counter
        self.counter: Account = Account.by("acc_name", self.counter)
        assert self.counter
        self.currency = self.counter.currency
        self.last_tx = None

    def process(self, line):
        line = super(PaypalReader, self).process(line)
        if line is None:
            return None

        if line.description == "General Currency Conversion":
            if self.last_tx:
                if line.currency == self.currency and self.last_tx.currency != self.currency:
                    self.last_tx.amount = line.amount
                if self.amount is not None and self.last_tx.contact and self.last_tx.currency:
                    ret = self.last_tx
                    ret.type = "D"
                    self.last_tx = None
                    return ret
                else:
                    return None
            else:
                self.last_tx = line
                self.last_tx.contact = None
                self.last_tx.description = None
                if self.last_tx.currency == self.currency:
                    self.last_tx.currency = None
                else:
                    self.last_tx.amount = None
                return None
        elif line.description.startswith("Bank Deposit to PP Account"):
            line.foreign_amount = line.amount
            line.type = "T"
            line.counter = self.counter
            return line
        elif line.description in ("Funds Receivable", "Funds Payable"):
            return None
        elif self.last_tx is not None:
            self.last_tx.description = line.description
            self.last_tx.contact = line.contact
            self.last_tx.currency = line.currency
            self.last_tx.foreign_amount = line.amount
            ret = self.last_tx
            ret.type = "D"
            self.last_tx = None
            return ret
        elif line.currency != self.currency:
            line.foreign_amount = line.amount
            line.amount = None
            self.last_tx = line
            return None
        else:
            self.last_tx = None
            line.type = "D"
            return line


class Importer(QObject):
    imported = pyqtSignal(Account, str)

    def __init__(self):
        super(Importer, self).__init__()

    def execute(self, account, file_name):
        reader = gripe.resolve(account.importer)(account, file_name)
        reader.jobFinished.connect(self.import_finished)
        reader.jobError.connect(self.import_finished)
        reader.submit()

    def import_finished(self, reader):
        self.imported.emit(reader.account(), reader.filename)


class ScanInbox:
    def __init__(self):
        self.data_dir = os.path.join(gripe.root_dir(), "bucks", "data")
        self.acc_name: str = None
        self.initial: str = None
        self.inbox: str = None
        self.queue: str = None
        self.done: str = None
        self.errors: str = None
        self._accounts = {}
        self._load_initial()

    def _load_initial(self):
        with gripe.db.Tx.begin():
            with os.scandir(self.data_dir) as it:
                for entry in it:
                    if not entry.name.startswith('.') and entry.is_dir():
                        acc_name = entry.name
                        if self.set_account(acc_name):
                            file_names = gripe.listdir(self.queue)
                            for file_name in file_names:
                                logger.debug("ScanInbox: Deleting stale import file '%s' for account '%s'",
                                             file_name, acc_name)
                                os.remove(os.path.join(self.queue, file_name))
                            file_names = gripe.listdir(self.initial)
                            for file_name in file_names:
                                logger.debug("ScanInbox: Found initial file '%s' for account '%s'",
                                             file_name, acc_name)
                                shutil.copyfile(os.path.join(self.initial, file_name),
                                                os.path.join(self.inbox, file_name))

    def import_started(self, reader):
        pass

    def import_finished(self, reader):
        try:
            os.rename(reader.filename,
                      os.path.join(reader.acc_dir, "done", os.path.basename(reader.filename)))
        except FileExistsError:
            os.remove(reader.filename)

    def import_error(self, reader):
        try:
            os.rename(reader.filename,
                      os.path.join(reader.acc_dir, "error", os.path.basename(reader.filename)))
        except FileExistsError:
            os.remove(reader.filename)

    def addfile(self, file_name):
        account = Account.by("acc_name", self.acc_name)
        reader = gripe.resolve(account.importer, Reader)(account, file_name)
        reader.jobStarted.connect(self.import_started)
        reader.jobFinished.connect(self.import_finished)
        reader.jobError.connect(self.import_error)
        self.addjob(reader)

    def addfiles(self, filenames):
        for f in filenames:
            self.addfile(f)

    def set_account(self, acc_name):
        account = self._accounts.get(acc_name)
        if account is None:
            account = Account.by("acc_name", acc_name)
            if account is not None:
                self._accounts[acc_name] = account
        if account is not None:
            self.acc_name = acc_name
            self.initial = os.path.join(self.data_dir, acc_name, "initial")
            gripe.mkdir(self.initial)
            self.inbox = os.path.join(self.data_dir, acc_name, "inbox")
            gripe.mkdir(self.inbox)
            self.queue = os.path.join(self.data_dir, acc_name, "queue")
            gripe.mkdir(self.queue)
            self.done = os.path.join(self.data_dir, acc_name, "done")
            gripe.mkdir(self.done)
            self.errors = os.path.join(self.data_dir, acc_name, "error")
            gripe.mkdir(self.errors)
        return account is not None

    def run(self):
        with gripe.db.Tx.begin():
            with os.scandir(self.data_dir) as it:
                for entry in it:
                    if not entry.name.startswith('.') and entry.is_dir():
                        acc_name = entry.name
                        if self.set_account(acc_name):
                            # logger.debug("ScanInbox: Scanning inbox for account '%s'", acc_name)
                            file_names = gripe.listdir(self.inbox)
                            for file_name in file_names:
                                logger.debug("ScanInbox: Found file %s for account '%s'", file_name, acc_name)
                                os.rename(os.path.join(self.inbox, file_name), os.path.join(self.queue, file_name))
                                self.addfile(os.path.join(self.queue, file_name))


class ScanInboxPlugin(grumpy.bg.bg.ThreadPlugin):
    def __init__(self, thread):
        super(ScanInboxPlugin, self).__init__(thread)
        self.scaninbox = ScanInbox()

    def run(self):
        self.scaninbox.run()
