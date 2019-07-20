#
# Copyright (c) 2019 Jan de Visser (jan@sweattrails.com)
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


import argparse
import readline

import gripe.db
import grumble.model

import bucks.schema

from bucks.datamodel.account import Account
from bucks.datamodel.account import Transaction
from bucks.datamodel.institution import Institution
from bucks.datamodel.category import Category
from bucks.datamodel.contact import Contact


def get_contact():
    contact = Contact.by("contact_name", "Vincenzo's")
    print(contact.contact_name)
    return contact


def get_transactions(account=None, contact=None):
    q = Transaction.query(keys_only=False, include_subclasses=True, raw=True)
    if contact:
        q.add_filter("contact", contact)
    if account:
        q.add_parent_join(Account)
    print(q)
    ret = []
    for res in q:
        ret.append(res)
        print(res)
    return ret


def get_categories():
    todo = []
    ret = []
    objects = {}

    def handle(obj):
        pk = obj['category."_parent"']
        key = obj['category."_key"']
        if not pk:
            ret.append(obj)
            objects[key] = obj
            obj["parent"] = None
            obj["cum_debit"] = obj["total_debit"]
            obj["cum_credit"] = obj["total_credit"]
            obj["cum_total"] = obj["total"]
        else:
            parent = objects.get(pk)
            if parent:
                objects[key] = obj
                obj["parent"] = parent
                parent["subcategories"].append(obj)
                p = parent
                while p:
                    p["cum_debit"] += obj["total_debit"]
                    p["cum_credit"] += obj["total_credit"]
                    p["cum_total"] += obj["total"]
                    p = p["parent"]
            else:
                todo.append(obj)

    q = Transaction.query(keys_only=False, include_subclasses=True, raw=True)
    q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
    q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
    q.add_aggregate("debit", name="total_debit", groupby=Category, func="SUM")
    q.add_aggregate("credit", name="total_credit", groupby=Category, func="SUM")
    q.add_aggregate("amt", name="total", groupby=Category, func="SUM")
    q.add_join(Category, "category", jointype="RIGHT")

    with gripe.db.Tx.begin():
        for o in q:
            o["subcategories"] = []
            if o["total_debit"] is None:
                o["total_debit"] = 0.0
            if o["total_credit"] is None:
                o["total_credit"] = 0.0
            if o["total"] is None:
                o["total"] = 0.0
            o["cum_debit"] = 0.0
            o["cum_credit"] = 0.0
            o["cum_total"] = 0.0
            handle(o)
    while todo:
        handle(todo.pop(0))
    return ret


def amt(v):
    return "{:9.2f}".format(v) if v is not None and v != 0.0 else ''


def list_categories():
    def list_category(indent, cat):
        print("{0:30s}|{1:>10s}|{2:>10s}|{3:>10s}|{4:>10s}|{5:>10s}|{6:>10s}|".
              format((" " * indent) + cat['category."cat_name"'],
                     amt(cat['total_debit']),
                     amt(cat['total_credit']),
                     amt(cat['total']),
                     amt(cat['cum_debit']),
                     amt(cat['cum_credit']),
                     amt(cat['cum_total'])
                     )
              )
        for sub in cat["subcategories"]:
            list_category(indent+2, sub)

    with gripe.db.Tx.begin():
        cats = get_categories()
        if cats:
            print("{0:^30s}|{1:^10s}|{2:^10s}|{3:^10s}|{4:^10s}|{5:^10s}|{6:^10s}|".format(
                "Category", "Debit", "Credit", "Total", "Cum.Debit", "Cum.Credit", "Cum.Total"))
            print("{0:s}+{1:s}+{1:s}+{1:s}+{1:s}+{1:s}+{1:s}+".format("-" * 30, "-" * 10))
            total = {
                    "total_debit": 0.0, "total_credit": 0.0, "total": 0.0,
                    "cum_debit": 0.0, "cum_credit": 0.0, "cum_total": 0.0,
                    'category."cat_name"': 'Total', "subcategories": []
                    }
            for cat in cats:
                list_category(0, cat)
                total["cum_debit"] += cat["cum_debit"]
                total["cum_credit"] += cat["cum_credit"]
                total["cum_total"] += cat["cum_total"]
            list_category(0, total)

# -- A C C O U N T S --------------------------------------------------------


def list_transactions(account):
    pass


def get_accounts():
    ret = []
    q = Transaction.query(keys_only=False, include_subclasses=True, alias="account")
    q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
    q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
    q.add_aggregate("debit", name="total_debit", groupby=Account, func="SUM")
    q.add_aggregate("credit", name="total_credit", groupby=Account, func="SUM")
    q.add_aggregate("amt", name="total", groupby=Account, func="SUM")
    q.add_parent_join(Account, "account")

    with gripe.db.Tx.begin():
        for o in q:
            # if o.total_debit is None:
            #     o.total_debit = 0.0
            # if o.total_credit is None:
            #     o.total_credit = 0.0
            # if o.total is None:
            #     o.total = 0.0
            ret.append(o)
    return ret


def list_accounts():
    def list_account(acc):
        print("{0:30s}|{1:>10s}|{2:>10s}|{3:>10s}|".
              format(acc.acc_name,
                     amt(acc.total_debit),
                     amt(acc.total_credit),
                     amt(acc.total)
                     )
              )

    with gripe.db.Tx.begin():
        accounts = get_accounts()
        if accounts:
            print("{0:^30s}|{1:^10s}|{2:^10s}|{3:^10s}|".format(
                "Account", "Debit", "Credit", "Total"))
            print("{0:s}+{1:s}+{1:s}+{1:s}+".format("-" * 30, "-" * 10))
            total = Account(parent=None, acc_name='Total', total_debit=0.0, total_credit=0.0, total=0.0)
            choices = {}
            num = 1
            for account in accounts:
                list_account(account)
                total.total_debit += account.total_debit
                total.total_credit += account.total_credit
                total.total += account.total
            list_account(total)


def get_account(account: str) -> Account:
    if account is None:
        account = ''
    accounts = get_accounts()
    choices = {}
    num = 1
    for acc in accounts:
        if not account or acc.acc_name.upper().startswith(account.upper()):
            choices[num] = acc
            num += 1
    if not choices:
        return None
    elif len(choices) == 1:
        return choices[1]
    else:
        for ix in range(1, num):
            print("{0:3d}. {1}".format(ix, choices[ix].acc_name))
        ix = None
        while ix is None:
            ix = input("? ")
            if ix == 'b':
                ix = -1
            else:
                try:
                    ix = int(ix)
                except ValueError:
                    ix = -1
                if (ix > 0) and ix not in choices:
                    ix = None
        if ix <= 0:
            return None
        else:
            return choices[ix]


def edit_account(account: Account) -> None:
    done = False
    while not done:
        print("{0:20s} {1}".format("Name:", account.acc_name))
        global readline_inject
        readline_inject = account.description
        descr = input("Description: ")

        cmd_ok = False
        cmd = None
        while not cmd_ok:
            cmd = input("Save (y/n/r)? ")
            cmd_ok = cmd in ('y', 'n', 'r')
        if cmd == 'y':
            with gripe.db.Tx.begin():
                account.description = descr
                account.put()
            done = True
        else:
            done = cmd == 'n'


def view_account(account):
    account: Account = get_account(account)

    def _view_account():
        if account:
            with gripe.db.Tx.begin():
                inst: Institution = account.parent()
                print("{0:20s} {1}".format("Name:", account.acc_name))
                print("{0:20s} {1}".format("Institution:", inst.inst_name))
                print("{0:20s} {1}".format("Account Number:", account.acc_nr))
                print("{0:20s} {1}".format("Description:", account.description))
                print()
                print("{0:20s} {1:>10.2f}".format("Opening Balance:", account.opening_balance))
                print("{0:20s} {1}".format("Opening Date:", account.opening_date))
                print("{0:20s} {1:>10.2f}".format("Current Balance:", account.current_balance))

        _view_account()
        ok = True
        while ok:
            cmd = input(account.acc_name + "> ")
            if cmd == 'b':
                ok = False
            elif cmd == 't':
                list_transactions(account)
            elif cmd == 'e':
                edit_account(account)
            elif cmd == 'v':
                _view_account()


def account_menu(subcmd):
    if not subcmd:
        print("-- A C C O U N T S --")
    ok = True
    while ok:
        cmd = input("Accounts> ").strip() if not subcmd else subcmd
        subcmd = None
        cmd = cmd.split(' ', 1)
        if cmd[0] == 'l':
            list_accounts()
        elif cmd[0] == 'v':
            view_account(cmd[1] if len(cmd) > 1 else None)
        elif cmd[0] == 'b':
            ok = False

# -- F I L E  I M P O R T S -------------------------------------------------


def file_import(account, file_name):
    pass


readline_inject = None


def pre_input_hook():
    global readline_inject
    if readline_inject:
        readline.insert_text(readline_inject)
        readline.redisplay()
        readline_inject = None


def mainloop(cmd):
    readline.set_pre_input_hook(pre_input_hook)

    ok = True
    while ok:
        cmd = cmd.strip() if cmd else None
        if not cmd:
            cmd = input("*> ").strip()
        cmd = cmd.split(' ', 1)
        if cmd[0] == 'a':
            account_menu(" ".join(cmd[1:]) if len(cmd) > 1 else None)
        elif cmd[0] == 'c':
            list_categories()
        elif cmd[0] == 'q':
            ok = False
        cmd = None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--clear", action="store_true", help="Erase all data")
    parser.add_argument("-s", "--schema", type=str, help="Use the given file as the initial schema")
    parser.add_argument("-i", "--imp", type=str, help="Import the given transactions file")
    parser.add_argument("commands", type=str, nargs="*", help="Commands string")

    cmdline = parser.parse_args()

    ok = True
    with gripe.db.Tx.begin():
        if cmdline.clear:
            gripe.db.Tx.reset_schema(True)
        if Account.query().get() is None:
            if cmdline.schema:
                bucks.schema.SchemaImporter.import_file(cmdline.schema)
            # else:
            #     wizard = bucks.wizard.FirstUse()
            #     ok = wizard.exec_()
        if cmdline.imp:
            acc, file_name = cmdline.imp.split(':', 2)
            account = Account.by("acc_name", acc)
            assert account
            import_file(account, file_name)

    if ok:
        mainloop(" ".join(cmdline.commands) if cmdline.commands else None)


main()
