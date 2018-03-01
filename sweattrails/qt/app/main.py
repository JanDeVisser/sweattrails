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

import argparse
import sys
import traceback

# sys.path.append(".")

import gripe.db
import sweattrails.qt.app.commandline
import sweattrails.qt.app.gui

logger = gripe.get_logger(__name__)


# ============================================================================
# Parse command line
# ============================================================================

parser = argparse.ArgumentParser()
parser.add_argument("-d", "--download", action="store_true",
                    help="Download new activities from your Garmin device over ANT+")
parser.add_argument("-i", "--import", dest="imp", type=str, nargs="+",
                    help="Import the given file")
parser.add_argument("-W", "--withings", action="store_true",
                    help="Download Withings data")
parser.add_argument("-u", "--user", type=str,
                    help="""Username to log in as. Note that this overrides a possibly stored username""")
parser.add_argument("-P", "--password", type=str,
                    help="""Password to use when logging in. Only used when -u/--user is specified as well.
Note that this overrides a possibly stored password.""")
parser.add_argument("-S", "--savecredentials", action="store_true")
parser.add_argument("-s", "--session", type=str,
                    help="""Open the session with the given ID""")
parser.add_argument("-t", "--tab", type=int,
                    help="""Focus on the tab with the given index""")
parser.set_defaults(savecredentials=False, download=False)

cmdline = parser.parse_args()
if cmdline.user:
    assert cmdline.password, "--password option requires --user option"
if cmdline.savecredentials:
    assert cmdline.user and cmdline.password, \
        "--savecredentials option requires --user and --password option"


# ============================================================================
# Build Application objects based on command line:
# ============================================================================

appcls = sweattrails.qt.app.commandline.SweatTrailsCmdLine \
    if cmdline.imp or cmdline.download or cmdline.withings \
    else sweattrails.qt.app.gui.SweatTrails
app = appcls(sys.argv)
app.start(cmdline)

try:
    if cmdline.imp:
        app.file_import(cmdline.imp)

    if cmdline.download:
        app.download()

    if cmdline.withings:
        app.withings()


except Exception as e:
    print(e)
    traceback.print_exc()

app.exec_()
