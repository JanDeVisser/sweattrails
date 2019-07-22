#
#   Copyright (c) 2019 Jan de Visser (jan@sweattrails.com)
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the Free
#   Software Foundation; either version 2 of the License, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#   more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc., 51
#   Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

import grumble.property

import bucks.datamodel.balance


class Category(grumble.model.Model):
    cat_name = grumble.property.TextProperty(verbose_name="Category", is_label=True)
    description = grumble.property.TextProperty()
    current_balance = bucks.datamodel.balance.EntityBalance(verbose_name="Current Balance", entity_prop="category")

    def transactions(self):
        from bucks.datamodel.account import Account
        from bucks.datamodel.account import Transaction
        q = Transaction.query(keys_only=False)
        q.add_parent_join(Account)
        q.add_filter("category", "->", self)
        return q

    @classmethod
    def get_categories(cls):
        from bucks.datamodel.account import Transaction
        q = Transaction.query(keys_only=False, include_subclasses=True, alias="category")
        q.add_synthetic_column("debit", "(CASE WHEN amt < 0 THEN -amt ELSE 0 END)")
        q.add_synthetic_column("credit", "(CASE WHEN amt > 0 THEN amt ELSE 0 END)")
        q.add_aggregate("k.debit", name="total_debit", groupby=Category, func="SUM")
        q.add_aggregate("k.credit", name="total_credit", groupby=Category, func="SUM")
        q.add_aggregate("k.amt", name="total", groupby=Category, func="SUM")
        q.add_join(Category, "category", jointype="RIGHT")
        return q
