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
import datetime
import readline
import traceback

import gripe
import gripe.db
import grumble.model
import grumble.property

import bucks.schema
import bucks.tximport

from bucks.datamodel.account import Account
from bucks.datamodel.account import Transaction
from bucks.datamodel.institution import Institution
from bucks.datamodel.category import Category
from bucks.datamodel.contact import Contact


# -- C O M M A N D  L I N E  I N P U T ---------------------------------------

readline_inject = None


def pre_input_hook():
    global readline_inject
    if readline_inject:
        readline.insert_text(readline_inject)
        readline.redisplay()
        readline_inject = None


class CommandLineCommand:
    def __init__(self, cmd, processor, help):
        self.cmd = cmd
        self.processor = processor
        self.help = help


class CommandLineProcessor:
    def __init__(self, prompt="*", preprocessor=None, help=None, banner=None, commands=()):
        if not hasattr(self, "_commands") or not self._commands or commands:
            self._commands = commands
        if not hasattr(self, "_help") or self._help is None or help is not None:
            self._help = help
        if not hasattr(self, "_preprocessor") or self._preprocessor is None or preprocessor is not None:
            self._preprocessor = preprocessor
        if not hasattr(self, "_prompt") or self._prompt is None or prompt is not None:
            self._prompt = prompt if prompt else banner.title() if banner else "*"
        if not hasattr(self, "_banner") or self._banner is None or banner is not None:
            self._banner = banner

    def printbanner(self, cmd):
        if self._banner:
            b = ""
            for c in self._banner(cmd) if callable(self._banner) else self._banner:
                b = b + c.upper() + " "
            b = "-- " + b
            b = b + '-' * (40 - len(b))
            print()
            print(b)
            print()

    def printhelp(self):
        if self._help:
            print(self._help)
        for c in self._commands:
            print("\t{0:<15}{1}".format(c.cmd + ":", c.help))
        print("\t{0:<15}Display this help".format("help:"))
        print("\t{0:<15}Up one menu".format("back:"))
        print("\t{0:<15}Quit application".format("quit:"))

    def __call__(self, cmd):
        with gripe.db.Tx.begin():
            self.printbanner(cmd)
            if self._preprocessor:
                cmd = self._preprocessor(cmd)
                if isinstance(cmd, bool) and not cmd:
                    return True
        ok: bool = True
        ret: bool = True
        while ok:
            if not cmd:
                cmd = input((self._prompt() if callable(self._prompt) else self._prompt) + "> ").strip()
                cmd = cmd.split(' ')
            command = cmd[0].lower()
            if "back".startswith(command):
                ok = False
                ret = True
            elif "quit".startswith(command):
                ok = False
                ret = False
            elif "help".startswith(command):
                self.printhelp()
                cmd = None
            for c in self._commands:
                if c.cmd.startswith(command):
                    try:
                        with gripe.db.Tx.begin():
                            newcmd = c.processor(cmd[1:] if len(cmd) > 1 else None)
                            cmd = cmd[1:] if len(cmd) > 1 else None
                            if isinstance(newcmd, bool):
                                ok = newcmd
                            elif newcmd is None:
                                ok = True
                            elif type(newcmd, (list, tuple)):
                                cmd = newcmd
                                ok = True
                    except Exception as e:
                        traceback.print_exc()
                        ret = False
                    if not ret:
                        ok = False
                    break
        return ret


def amt(v):
    return "{:9.2f}".format(v) if v is not None and v != 0.0 else ''


def table_header(*columns):
    names = [name for name, width in columns]
    widths = [width for name, width in columns]
    fmt = "|".join("{{:^{:d}s}}".format(w) for w in widths) + "|"
    print(fmt.format(*names))
    fmt = "+".join("{{0:-<{:d}s}}".format(w) for w in widths) + "+"
    print(fmt.format(''))


# ----------------------------------------------------------------------------

def get_categories():
    todo = []
    ret = []
    objects = {}

    def handle(obj):
        pk = obj.parent_key()
        key = obj.key()
        if not pk:
            ret.append(obj)
            objects[key] = obj
            obj.cum_debit = obj.total_debit
            obj.cum_credit = obj.total_credit
            obj.cum_total = obj.total
        else:
            parent = objects.get(pk)
            if parent:
                objects[key] = obj
                parent.subcategories.append(obj)
                p = parent
                while p:
                    p.cum_debit += obj.total_debit
                    p.cum_credit += obj.total_credit
                    p.cum_total += obj.total
                    p = p.parent()
            else:
                todo.append(obj)

    try:
        q = Category.get_categories()

        with gripe.db.Tx.begin():
            for o in q:
                o.add_adhoc_property("subcategories", grumble.property.ListProperty(default=[]))
                o.add_adhoc_property("cum_debit", grumble.property.FloatProperty(), 0.0)
                o.add_adhoc_property("cum_credit", grumble.property.FloatProperty(), 0.0)
                o.add_adhoc_property("cum_total", grumble.property.FloatProperty(), 0.0)
                handle(o)
        while todo:
            handle(todo.pop(0))
    except Exception as e:
        raise
    return ret


def list_categories():
    def list_category(indent, cat):
        print("{0:30s}|{1:>10s}|{2:>10s}|{3:>10s}|{4:>10s}|{5:>10s}|{6:>10s}|".
              format((" " * indent) + cat.cat_name,
                     amt(cat.total_debit),
                     amt(cat.total_credit),
                     amt(cat.total),
                     amt(cat.cum_debit),
                     amt(cat.cum_credit),
                     amt(cat.cum_total)
                     )
              )
        for sub in cat.subcategories:
            list_category(indent+2, sub)

    with gripe.db.Tx.begin():
        cats = get_categories()
        if cats:
            table_header(("Category", 30), ("Debit", 10), ("Credit", 10), ("Total", 10),
                         ("Cum.Debit", 10), ("Cum.Credit", 10), ("Cum.Total", 10))
            total = Category(parent=None, cat_name='Total', subcategories=[],
                             total_debit=0.0, total_credit=0.0, total=0.0)
            total.add_adhoc_property("cum_debit", grumble.property.FloatProperty(default=0.0))
            total.add_adhoc_property("cum_credit", grumble.property.FloatProperty(default=0.0))
            total.add_adhoc_property("cum_total", grumble.property.FloatProperty(default=0.0))
            for cat in cats:
                list_category(0, cat)
                total.cum_debit += cat.cum_debit
                total.cum_credit += cat.cum_credit
                total.cum_total += cat.cum_total
            list_category(0, total)


def list_categories_preprocessor(cmd):
    if cmd:
        cmd.insert(0, "v")
    else:
        list_categories()
    return cmd


class ViewCategoryProcessor(CommandLineProcessor):
    def __init__(self, *args, **kwargs):
        super(ViewCategoryProcessor, self).__init__(*args, **kwargs)
        self._help = "Category details"
        self._commands = (
            CommandLineCommand(cmd="view", processor=self.view, help="View category details"),
            CommandLineCommand(cmd="edit", processor=self.edit, help="Edit category details"),
            CommandLineCommand(cmd="transactions", processor=self.transactions, help="List category transactions"),
        )
        self._category = None

    def category(self, category=None):
        if not self._category:
            categories = Category.get_categories()
            choices = {}
            num = 1
            for cat in categories:
                if not category or cat.cat_name.upper().startswith(category.upper()):
                    choices[num] = cat
                    num += 1
            if not choices:
                self._category = None
            elif len(choices) == 1:
                self._category = choices[1]
            else:
                for ix in range(1, num):
                    print("{0:3d}. {1}".format(ix, choices[ix].cat_name))
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
                    self._category = None
                else:
                    self._category = choices[ix]
        return self._category

    def view(self, dummy=None):
        if self.category():
            category = self.category()
            with gripe.db.Tx.begin():
                inst: Institution = category.parent()
                print("{0:20s} {1}".format("Name:", category.cat_name))
                print("{0:20s} {1}".format("Description:", category.description))
                print()
                print("{0:20s} {1:>10.2f}".format("Current Balance:", category.current_balance))
        return dummy

    def edit(self, dummy=None) -> None:
        done = False
        category = self.category()
        while not done:
            print("{0:20s} {1}".format("Name:", category.cat_name))
            global readline_inject
            readline_inject = category.description
            descr = input("Description: ")

            cmd_ok = False
            cmd = None
            while not cmd_ok:
                cmd = input("Save (y/n/r)? ")
                cmd_ok = cmd in ('y', 'n', 'r')
            if cmd == 'y':
                with gripe.db.Tx.begin():
                    category.description = descr
                    category.put()
                done = True
            else:
                done = cmd == 'n'
        return dummy

    def transactions(self, dummy=None):
        def print_tx(tx: Transaction):
            print("{0:%Y-%m-%d}|{1:20s}|{2:50s}|{3:>10s}|{4:>10s}|{5:20s}|{6:30s}|".
                  format(tx.date,
                         tx.joined_value("p.acc_name"),
                         tx.description,
                         amt(tx.debit),
                         amt(tx.credit),
                         tx.joined_value("project.proj_name"),
                         tx.joined_value("contact.contact_name"),
                         )
                  )

        if self.category():
            category: Category = self.category()
            with gripe.db.Tx.begin():
                txs = category.transactions()
                if txs:
                    print("{0:^10s}|{1:^20s}|{2:^50s}|{3:^10s}|{4:^10s}|{5:^20s}|{6:^30s}|".format(
                        "Date", "Account", "Description", "Debit", "Credit", "Project", "Contact"))
                    print("{0:-<10s}+{0:-<20s}+{0:-<50s}+{0:-<10s}+{0:-<10s}+{0:-<20s}+{0:-<30s}+".format(''))
                    total = Transaction(parent=None, date=datetime.date.today(), description='Total', debit=0.0, credit=0.0)
                    for tx in txs:
                        print_tx(tx)
                        total.debit += tx.debit
                        total.credit += tx.credit
                    print_tx(total)
        return dummy

    def _preprocessor(self, cmd):
        self._account = None
        self.account(cmd[0] if cmd else None)
        if cmd:
            cmd = cmd[1:]
        self.view_account()
        return cmd if self._account else False

    def _prompt(self):
        return self.account().acc_name

    def _banner(self, cmd):
        if self._account:
            return "Account " + self.account().acc_name
        else:
            return "Select account"


viewcategory = ViewCategoryProcessor()

categoryloop = CommandLineProcessor(banner="Categories", help="Display categories",
                                    preprocessor=list_categories_preprocessor,
                                    commands=(CommandLineCommand(cmd="list", processor=list_categories,
                                                                help="List all categories"),
                                             CommandLineCommand(cmd="view", processor=viewcategory,
                                                                help="View details of an account"),))

# -- A C C O U N T S --------------------------------------------------------


def list_accounts(dummy=None):
    def list_account(acc):
        print("{0:30s}|{1:>10s}|{2:>10s}|{3:>10s}|".
              format(acc.acc_name,
                     amt(acc.total_debit),
                     amt(acc.total_credit),
                     amt(acc.total)
                     )
              )

    with gripe.db.Tx.begin():
        accounts = Account.get_accounts()
        if accounts:
            table_header(("Account", 30), ("Debit", 10), ("Credit", 10), ("Total", 10))
            total = Account(parent=None, acc_name='Total', total_debit=0.0, total_credit=0.0, total=0.0)
            for account in accounts:
                list_account(account)
                total.total_debit += account.total_debit
                total.total_credit += account.total_credit
                total.total += account.total
            list_account(total)
    return True


def list_accounts_preprocessor(cmd):
    if cmd:
        cmd.insert(0, "v")
    else:
        list_accounts()
    return cmd


class ViewAccountProcessor(CommandLineProcessor):
    def __init__(self, *args, **kwargs):
        super(ViewAccountProcessor, self).__init__(*args, **kwargs)
        self._help = "Account details"
        self._commands = (
            CommandLineCommand(cmd="view", processor=self.view_account, help="View account details"),
            CommandLineCommand(cmd="edit", processor=self.edit_account, help="Edit account details"),
            CommandLineCommand(cmd="transactions", processor=self.list_transactions, help="List account transactions"),
            CommandLineCommand(cmd="import", processor=self.import_file, help="Import account transactions"),
        )
        self._account = None

    def account(self, account=None):
        if not self._account:
            accounts = Account.get_accounts()
            choices = {}
            num = 1
            for acc in accounts:
                if not account or acc.acc_name.upper().startswith(account.upper()):
                    choices[num] = acc
                    num += 1
            if not choices:
                self._account = None
            elif len(choices) == 1:
                self._account = choices[1]
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
                    self._account = None
                else:
                    self._account = choices[ix]
        return self._account

    def view_account(self, dummy=None):
        if self.account():
            account = self.account()
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
        return True

    def edit_account(self) -> None:
        done = False
        account = self.account()
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

    def import_file(self, cmd):
        if cmd:
            args = map(lambda file_name: (self.account(), file_name), cmd)
        else:
            file_name = input("File name?")
            args = ((self.account(), file_name),)
        import_files(args)

    def list_transactions(self, dummy=None):
        def print_tx(tx: Transaction):
            print("{0:%Y-%m-%d}|{1:50s}|{2:>10s}|{3:>10s}|{4:20s}|{5:20s}|{6:30s}|".
                  format(tx.date,
                         tx.description,
                         amt(tx.debit),
                         amt(tx.credit),
                         tx.category.cat_name if tx.category else '',
                         tx.project.proj_name if tx.project else '',
                         tx.contact.contact_name if tx.contact else '',
                         )
                  )

        if self.account():
            account: Account = self.account()
            with gripe.db.Tx.begin():
                txs = account.transactions()
                if txs:
                    table_header(("Date", 10), ("Description", 50), ("Debit", 10), ("Credit", 10),
                                 ("Category", 20), ("Project", 20), ("Contact", 30))
                    total = Transaction(parent=None, date=datetime.date.today(), description='Total',
                                        debit=0.0, credit=0.0)
                    for tx in txs:
                        print_tx(tx)
                        total.debit += tx.debit
                        total.credit += tx.credit
                    print_tx(total)
        return dummy

    def _preprocessor(self, cmd):
        self._account = None
        self.account(cmd[0] if cmd else None)
        if cmd:
            cmd = cmd[1:]
        self.view_account()
        return cmd if self._account else False

    def _prompt(self):
        return self.account().acc_name

    def _banner(self, cmd):
        if self._account:
            return "Account " + self.account().acc_name
        else:
            return "Select account"


viewaccount = ViewAccountProcessor()

accountloop = CommandLineProcessor(banner="Accounts", help="Display accounts", preprocessor=list_accounts_preprocessor,
                                   commands=(CommandLineCommand(cmd="list", processor=list_accounts,
                                                                help="List all accounts"),
                                             CommandLineCommand(cmd="view", processor=viewaccount,
                                                                help="View details of an account"),))

# -- F I L E  I M P O R T S -------------------------------------------------

import_thread = None


def import_files(args):
    global import_thread
    if import_thread is None:
        import_thread = bucks.tximport.ScanInboxThread()
    if not args:
        acc = input("Account?")
        file_name = input("File name?")
        args = (acc + ":" + file_name,)
    for arg in args:
        if isinstance(arg, str):
            acc, file_name = arg.split(':', 2)
        elif isinstance(arg, (list, tuple)):
            acc, file_name = arg
        else:
            print("Cannot import " + arg)
            continue
        if not gripe.exists(file_name):
            print("File '%s' does not exist.".format(file_name))
            continue
        account = Account.by("acc_name", acc) if not isinstance(acc, Account) else acc
        if account is None:
            print("Account '%s' does not exist.".format(acc))
            continue
        import_thread.addfile(account, file_name)
    return True


mainloop = CommandLineProcessor(help="The main help text",
                    banner="Main menu", prompt="*",
                                commands=(CommandLineCommand(cmd="account", processor=accountloop, help="Accounts"),
                                          CommandLineCommand(cmd="category", processor=categoryloop, help="Categories"),
                                          CommandLineCommand(cmd="import", processor=import_files,
                                                             help="Import transaction files")))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--clear", action="store_true", help="Erase all data")
    parser.add_argument("-s", "--schema", type=str, help="Use the given file as the initial schema")
    parser.add_argument("-i", "--imp", type=str, nargs="+", help="Import the given transactions file")
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
            import_files(cmdline.imp)

    if ok:
        global import_thread
        if import_thread is None:
            import_thread = bucks.tximport.ScanInboxThread()
        readline.set_pre_input_hook(pre_input_hook)
        try:
            mainloop(cmdline.commands)
        except Exception as e:
            traceback.print_exc()


main()
