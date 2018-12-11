#
# Copyright (c) 2012-2014 Jan de Visser (jan@sweattrails.com)
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

import re
import io
import xml.sax

__author__ = "jan"
__date__ = "$12-Mar-2012 10:34:48 AM$"

class Path():
    def __init__(self, path):
        self.path = path
        self.re = re.compile(path)
        self.start = []
        self.end = []
        self.text = []

    def matches(self, path):
        return (path == self.path) or self.re.search(path)

    def add_start(self, f):
        self.start.append(f)

    def add_end(self, f):
        self.end.append(f)

    def add_text(self, f):
        self.text.append(f)

    def on_start(self, context):
        for f in self.start:
            f(context)

    def on_text(self, context, t):
        for f in self.text:
            f(context, t)

    def on_end(self, context):
        for f in self.end:
            f(context)


class XMLProcessor(xml.sax.handler.ContentHandler):
    class XMLProcessorContext():
        pass

    class ContentHandler(xml.sax.handler.ContentHandler):
        def __init__(self, processor, context):
            self.processor = processor
            if not(context):
                context = processor.XMLProcessorContext()
            self.context = context
            self.context.xmlp_level = 0
            self.context.xmlp_stack = []
            self.context.xmlp_current = None
            self.context.xmlp_cycle = 0
            self.context.xmlp_repeat = True

        def startDocument(self):
            self.path = '/'
            self.processor.run_start(self.context, self.path)

        def endDocument(self):
            self.path = '/'
            self.processor.run_end(self.context, self.path)

        def startElement(self, name, attrs):
            self.context.xmlp_level += 1
            self.context.xmlp_stack.append(name)
            self.context.xmlp_current = name
            if not(self.path.endswith('/')):
                self.path += '/'
            self.path += name
            self.opened = name
            self.textbuffer = ''
            self.processor.run_start(self.context, self.path)
            for attr in attrs.getNames():
                self.processor.run_attr(self.context, self.path, attr, attrs.getValue(attr))

        def endElement(self, name):
            self.context.xmlp_level -= 1
            self.context.xmlp_current = name
            if self.opened:
                self.processor.run_text(self.context, self.path, self.textbuffer.strip())
            self.processor.run_end(self.context, self.path)
            self.opened = None
            self.path = self.path[0:self.path.rindex('/')]
            if len(self.path) == 0:
                self.path = '/'
            if len(self.context.xmlp_stack) > 0:
                self.context.xmlp_stack = self.context.xmlp_stack[0:len(self.context.xmlp_stack)]

        def characters(self, chrs):
            if self.context.xmlp_current is not None:
                self.textbuffer = self.textbuffer + chrs

    def __init__(self):
        self.paths = {}

    def get_path(self, path):
        if path in self.paths:
            p = self.paths[path]
        else:
            p = Path(path)
            self.paths[path] = p
        return p

    def get_matching_paths(self, path):
        ret = []
        for p in self.paths:
            pobj = self.paths[p]
            if pobj.matches(path):
                ret.append(pobj)
        return ret

    def for_text_of(self, path):
        p = self.get_path(path)
        def gethandler(f):
            p.add_text(f)
            return f
        return gethandler

    def for_start_of(self, path):
        p = self.get_path(path)
        def gethandler(f):
            p.add_start(f)
            return f
        return gethandler

    def for_end_of(self, path):
        p = self.get_path(path)
        def gethandler(f):
            p.add_end(f)
            return f
        return gethandler

    def run_start(self, context, path):
        for p in self.get_matching_paths(path):
            p.on_start(context)

    def run_end(self, context, path):
        for p in self.get_matching_paths(path):
            p.on_end(context)

    def run_text(self, context, path, text):
        for p in self.get_matching_paths(path):
            p.on_text(context, text)

    def run_attr(self, context, path, attr, value):
        savecurrent = context.xmlp_current
        context.xmlp_current = "@" + attr
        for p in self.get_matching_paths(path + "/@" + attr):
            p.on_text(context, value)
        context.xmlp_current = savecurrent

    def process(self, xml_file_or_stream, context=None):
        ch = self.ContentHandler(self, context)
        while ch.context.xmlp_repeat:
            ch.context.xmlp_repeat = False
            xml.sax.parse(xml_file_or_stream, ch)
            ch.context.xmlp_cycle += 1

    def process_string(self, xmltext, context=None):
        return self.process(io.StringIO(xmltext), context)

if __name__ == "__main__":
    import os.path
    import gripe
    
    processor = XMLProcessor()

    @processor.for_start_of("^/[^@]+$")
    def elem_start(context):
        print((context.indent + context.xmlp_current + ":"))
        context.indent += "  "

    @processor.for_end_of("^/[^@]+$")
    def elem_end(context):
        if len(context.indent) >= 2:
            context.indent = context.indent[0:len(context.indent) - 2]
        print((context.indent + "/" + context.xmlp_current))

    @processor.for_text_of("^/[^@]+$")
    def elem_text(context, text):
        print((context.indent + text))

    @processor.for_start_of("^/$")
    def doc_start(context):
        print(" - - -  S T A R T - - -")
        context.indent = ""

    @processor.for_end_of("^/$")
    def doc_end(context):
        print(" - - -  E N D - - -")

    @processor.for_text_of("^/.+/@[a-zA-Z]+$")
    def attr_value(context, text):
        print((context.indent + "  " + context.xmlp_current + "=" + text))

    @processor.for_text_of("test/@quux")
    def quux(context, text):
        print((context.indent + "  The quux value is " + text))

    @processor.for_text_of("test/frob")
    def frob(context, text):
        print((context.indent + "The frob value is --" + text + "--"))

    fname = os.path.join(gripe.root_dir(), "..", "test", "test.xml")

    print("===== process(filename)")
    processor.process(fname)

    print("===== process(stream)")
    with open(fname) as f:
        processor.process(f)

    print("===== process_string()")
    with open(fname) as f:
        xmltext = f.read()
        processor.process_string(xmltext)
