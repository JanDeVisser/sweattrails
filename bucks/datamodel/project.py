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
import bucks.datamodel.category


class Project(grumble.model.Model):
    proj_name = grumble.property.TextProperty(verbose_name="Project", is_label=True)
    description = grumble.property.TextProperty()
    category = grumble.reference.ReferenceProperty(reference_class=bucks.datamodel.category.Category,
                                                   verbose_name="Default Category")
    current_balance = bucks.datamodel.balance.EntityBalance(verbose_name="Current Balance", entity_prop="project")
