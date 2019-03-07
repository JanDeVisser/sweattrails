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

import gripe

import bucks.datamodel


class SchemaImporter:
    def __init__(self):
        assert 0

    @staticmethod
    def categories(parent, categories):
        for (name, subcategories) in categories.items():
            cat = bucks.datamodel.Category(parent=parent, cat_name=name)
            cat.put()
            SchemaImporter.categories(cat, subcategories)

    @staticmethod
    def projects(parent, projects):
        for (name, subprojects) in projects.items():
            proj = bucks.datamodel.Project(parent=parent, proj_name=name)
            proj.put()
            SchemaImporter.projects(proj, subprojects)

    @staticmethod
    def accounts(inst, accounts):
        for account in accounts:
            name = account.get("acc_name")
            if name:
                acc = bucks.datamodel.Account(parent=inst,
                                              acc_name=name, acc_nr=account.get("acc_nr"),
                                              description=account.get("description", name),
                                              importer=account.get("importer"))
                acc.put()
                if "opening_date" in account or "opening_balance" in account:
                    acc.set_opening_balance(account.get("opening_balance"), account.get("opening_date"))

    @staticmethod
    def institutions(institutions):
        for institution in institutions:
            name = institution.get("inst_name")
            if name:
                inst = bucks.datamodel.Institution(inst_name=name,
                                                   description=institution.get("description", name))
                inst.put()
                SchemaImporter.accounts(inst, institution.get("accounts", []))

    @staticmethod
    def import_file(file_name):
        data = gripe.json_util.JSON.file_read(file_name)
        if data:
            SchemaImporter.institutions(data.get("institutions", []))
            SchemaImporter.categories(None, data.get("categories", []))
            SchemaImporter.projects(None, data.get("projects", []))
