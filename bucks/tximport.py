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

import codecs
import csv
import datetime
import os.path
import re
import sys
import traceback

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtCore import QObject

import gripe.db
import gripe.json_util
import grumpy.bg

import bucks.datamodel


class Line:
    def __init__(self, account):
        self.account = account
        self.date = None
        self.contact = None
        self.category = None
        self.project = None
        self.counter = None
        self.amount = None
        self.type = None
        self.descr = None

    @staticmethod
    def find_or_create(cls, prop, value):
        ret = None
        if value:
            ret = cls.query(**{prop: value}).get()
            if not ret:
                ret = cls(**{prop: value})
                ret.put()
        return ret

    def persist(self):
        contact = self.find_or_create(bucks.datamodel.Contact, "contact_name", self.contact)
        category = self.find_or_create(bucks.datamodel.Category, "cat_name", self.category)
        project = self.find_or_create(bucks.datamodel.Project, "proj_name", self.project)
        tx = bucks.datamodel.Transaction.for_type(self.account, self.type, date=self.date, amt=self.amount,
                                                  description=self.descr, contact=contact, category=category,
                                                  project=project)
        tx.put()
        if self.type == "T":
            acc = self.find_or_create(bucks.datamodel.Account, "acc_name", self.counter)
            tx.account = acc
            tx.put()


class Reader(grumpy.bg.Job):
    def __init__(self, account, file_name):
        super(Reader, self).__init__()
        self.account = account
        self.filename = file_name
        self.datefmt = None
        self.templates = []
        self.mapping = []
        self._parse_template()


    def _parse_template(self):
        data = gripe.json_util.JSON.file_read(os.path.join("bucks", "data", self.account.acc_name + ".json"))
        if data and "mapping" in data:
            for m in data.mapping:
                ix = data.mapping[m]
                while len(self.mapping) <= ix:
                    self.mapping.append(None)
                self.mapping[ix] = m
            self.datefmt = data.mapping.get("datefmt", "%m/%d/%Y")
        if data and "templates" in data:
            for t in data.templates:
                self.templates.append(dict(t))

    @staticmethod
    def parse_date(s):
        # 01/23/2019
        # TODO: Support self.datefmt
        m = re.fullmatch(r"(\d\d)/(\d\d)/(\d\d\d\d)", s)
        if not m:
            print("Date '%s' has invalid format" % s, file=sys.stderr)
            return None
        try:
            return datetime.date(int(m.group(3)), int(m.group(1)), int(m.group(2)))
        except ValueError:
            traceback.print_exc(file=sys.stderr)
            return None

    def handle(self):
        with codecs.open(self.filename, encoding="utf-8") as f:
            with gripe.db.Tx.begin():
                reader = csv.DictReader(f, self.mapping)
                for line in reader:
                    l = Line(self.account)
                    self.process(l, line)
                    if l and hasattr(l, "type") and l.type:
                        l.persist()

    def process(self, l, fields):
        l.date = self.parse_date(fields["date"])
        if not l.date:
            return None
        l.amount = float(fields["amount"])
        self._parse_descr(l, fields["description"])

    def _parse_descr(self, line, descr):
        for tpl in self.templates:
            m = re.search(tpl["template"], descr)
            if m:
                for k in (key for key in tpl if key != 'template'):
                    v = tpl[k]
                    if callable(v):
                        v = v(self.account)
                    setattr(line, k, tpl[k])
                line.descr = descr
                break
        if not line.type:
            line.type = "D"
            line.descr = descr
        return line


class Importer(QObject):
    imported = pyqtSignal(bucks.datamodel.Account, str)

    def __init__(self):
        super(Importer, self).__init__()

    def execute(self, account, file_name):
        reader = gripe.resolve(account.importer)(account, file_name)
        reader.jobFinished.connect(self.import_finished)
        reader.jobError.connect(self.import_finished)
        reader.submit()

    def import_finished(self, reader):
        self.imported.emit(reader.account(), reader.filename)
