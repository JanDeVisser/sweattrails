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

import argparse
import sys

import bucks.app.mainwindow

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--clear", action="store_true", help="Erase all data")
    parser.add_argument("-s", "--schema", type=str, help="Use the given file as the initial schema")
    parser.add_argument("-i", "--imp", type=str, help="Import the given transactions file")

    cmdline = parser.parse_args()

    app = bucks.app.mainwindow.Bucks(sys.argv)
    app.start(cmdline)

    app.exec_()
    sys.exit()
