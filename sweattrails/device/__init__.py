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

import os.path

import gripe

_parser_factories_by_ext = {
    "fit": "sweattrails.device.fitparser.FITParser",
    "tcx": "sweattrails.device.tcxparser.TCXParser"
}

_parser_factories = []

if ("sweattrails" in gripe.Config.app and
        "parsers" in gripe.Config.app.sweattrails):
    for i in gripe.Config.app.sweattrails.parsers:
        cls = i["class"]
        ext = i.get("extension")
        if ext:
            _parser_factories_by_ext[ext] = cls
        else:
            _parser_factories.append(cls)


def get_parser(filename):
    f = os.path.basename(filename)
    parser = None

    (_, _, extension) = f.rpartition(".")
    if extension:
        extension = extension.lower()
    factory = _parser_factories_by_ext.get(extension)
    if factory:
        factory = gripe.resolve(factory)
        if hasattr(factory, "create_parser"):
            parser = factory.create_parser(filename)
        else:
            parser = factory(filename)
    if not parser:
        for factory in _parser_factories:
            if hasattr(factory, "create_parser"):
                parser = factory.create_parser(filename)
            else:
                parser = factory(filename)
            if parser:
                break
    return parser
