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

import collections
import datetime

from PyQt5.QtCore import QPointF
from PyQt5.QtCore import Qt

from PyQt5.QtGui import QColor
from PyQt5.QtGui import QPainter
from PyQt5.QtGui import QPen
from PyQt5.QtGui import QPolygonF
from PyQt5.QtWidgets import QWidget

import gripe

logger = gripe.get_logger(__name__)


class DataSource(object):
    def __init__(self, **kwargs):
        logger.debug("DataSource.__init__ %s", type(self))
        self._records = kwargs.pop("records", None)
        super(DataSource, self).__init__(**kwargs)

    def __iter__(self):
        return iter(self.records())

    def __getitem__(self, key):
        return self.records()[key]

    def fetch(self):
        return []

    def records(self):
        if not self._records:
            self._records = self.fetch()
        return self._records


class QueryDataSource(DataSource):
    def __init__(self, **kwargs):
        logger.debug("QueryDataSource.__init__ %s", type(self));
        self.query = kwargs.pop("query")
        super(QueryDataSource, self).__init__(**kwargs)

    def fetch(self):
        return self.query.fetchall()


class Axis(object):
    _count = 0

    def __init__(self, **kwargs):
        if "min" in kwargs:
            self._min = kwargs.pop("min")
        if "max" in kwargs:
            self._max = kwargs.pop("max")
        if "value" in kwargs:
            self._value = kwargs.pop("value")
        if "padding" in kwargs:
            self._padding = kwargs.pop("padding")
        if "offset" in kwargs:
            self._offset = kwargs.pop("offset")
        self._prop = kwargs.pop("property", None)
        self._name = kwargs.pop("name", None)
        self._smooth = kwargs.pop("smooth", None)
        if not self._name:
            self._name = "anon-" + str(Axis._count)
            Axis._count += 1
        logger.debug("Axis.__init__ %s {%s}", type(self), kwargs);
        super(Axis, self).__init__(**kwargs)

    def __str__(self):
        return self._name

    def graph(self):
        return self._graph

    def setGraph(self, graph):
        self._graph = graph

    def padding(self):
        if hasattr(self, "_axis"):
            return self._axis.padding()
        if not hasattr(self, "_padding"):
            self._padding = 0.1
            logger.debug("%s.padding(): %s", self, self._padding)
        return self._padding if not callable(self._padding) else self._padding()

    def scale(self):
        if hasattr(self, "_axis"):
            return self._axis.scale()
        if not hasattr(self, "_scale"):
            self._scale = self.max() - self.offset()
            logger.debug("%s.scale(): %s", self, self._scale)
        return self._scale() if callable(self._scale) else self._scale

    def offset(self):
        if hasattr(self, "_axis"):
            return self._axis.offset()
        if not hasattr(self, "_offset"):
            self._offset = self.min()
            logger.debug("%s.offset(): %s", self, self._offset)
        return self._offset() if callable(self._offset) else self._offset

    def ordinal(self, value):
        return value

    def fromordinal(self, value):
        return value

    def min(self):
        if hasattr(self, "_axis"):
            return self._axis.min()
        if not hasattr(self, "_min"):
            self._min = None
            ix = 0
            for r in self:
                val = self.ordinal(self.value(r))
                if val is not None:
                    self._min = min(val, self._min) if self._min is not None else val
                ix += 1
            self._min = self._min or 0
            logger.debug("%s.min(): %s #=%s", self, self._min, ix)
        return self._min if not callable(self._min) else self._min()

    def max(self):
        if hasattr(self, "_axis"):
            return self._axis.max()
        if not hasattr(self, "_max"):
            self._max = None
            ix = 0
            for r in self:
                val = self.ordinal(self.value(r))
                if val is not None:
                    self._max = max(val, self._max) if self._max is not None else val
                ix += 1
            self._max = self._max or 0
            logger.debug("%s.max(): %s #=%s", self, self._max, ix)
        return self._max if not callable(self._max) else self._max()

    def value(self, record):
        if hasattr(self, "_value"):
            expr = self._value
        elif self._prop:
            if not hasattr(record, self._prop):
                raise Exception("Record has no property '%s'" % self._prop)
            expr = getattr(record, self._prop)
        else:
            expr = record
        v = expr if not callable(expr) else expr(record)
        v = v if v is not None else 0
        if hasattr(self, "_smooth") and self._smooth:
            self._running.append(v)
            ret = sum(self._running) / len(self._running)
        else:
            ret = v
        return ret

    def __call__(self, record):
        return self.ordinal(self.value(record))

    def __iter__(self):
        if hasattr(self, "_smooth") and self._smooth:
            self._running = collections.deque(maxlen=self._smooth)
        return iter(self.graph().datasource()) if self.graph().datasource() else iter([])


class DateAxis(Axis):
    def __init__(self, **kwargs):
        super(DateAxis, self).__init__(**kwargs)

    def ordinal(self, value):
        return value.toordinal() if isinstance(value, (datetime.date, datetime.datetime)) else None

    def fromordinal(self, ordvalue):
        return datetime.date.fromordinal(ordvalue)


class Series(Axis):
    def __init__(self, **kwargs):
        logger.debug("Series.__init__ %s", type(self));
        self._trendlines = []
        self._polygon = None
        self._color = kwargs.pop("color", None)
        self._style = kwargs.pop("style", None)
        self._shade = kwargs.pop("shade", None)
        self._layer = kwargs.pop("layer", 1)
        g = kwargs.pop("graph", None)
        if g:
            g.addSeries(self)
        self._visible = True
        super(Series, self).__init__(**kwargs)

    def setGraph(self, graph):
        super(Series, self).setGraph(graph)
        for tl in self._trendlines:
            tl.setGraph(graph)

    def graph(self):
        return self._graph

    def setAxis(self, axis):
        self._axis = axis
        self.setGraph(axis.graph())

    def xaxis(self):
        return self._graph.xaxis() if self._graph else None

    def datasource(self):
        return self._graph.ds() if self._graph else None

    def setColor(self, color):
        self._color = color

    def color(self):
        ret = self._color
        if not ret:
            ret = self._axis.color() if hasattr(self, "_axis") else Qt.black
        return ret

    def style(self):
        return self._style or self._axis.style() if hasattr(self, "_axis") else Qt.SolidLine

    def setStyle(self, style):
        self._style = style

    def setLayer(self, layer):
        self._layer = layer

    def layer(self):
        return self._axis.layer() if hasattr(self, "_axis") else self._layer

    def shade(self):
        return self._axis.shade() if hasattr(self, "_axis") else self._shade

    def setShade(self, shade):
        self._shade = bool(shade)

    def visible(self):
        return self._axis.visible() if hasattr(self, "_axis") else self._visible

    def setVisible(self, visible):
        self._visible = visible

    def hide(self):
        self.setVisible(False)

    def x(self, obj):
        return float(self.xaxis()(obj)) \
            if self.xaxis() and callable(self.xaxis()) \
            else float(obj)

    def y(self, obj):
        return float(self(obj))

    def polygon(self):
        if not self._polygon:
            xoffset = float(self.xaxis().offset()
                            if self.xaxis() and hasattr(self.xaxis(), "offset") and callable(self.xaxis().offset)
                            else 0)
            yoffset = float(self.offset())
            if yoffset != 0:
                yoffset -= self.scale() * float(self.padding())
            logger.debug("%s.polygon() xoffset %s yoffset %s", self, xoffset, yoffset)
            points = [QPointF(self.x(obj) - xoffset, self.y(obj) - yoffset) for obj in self]
            if self.shade() is not None:
                points.insert(0, QPointF(points[0].x(), 0))
                points.append(QPointF(points[-1].x(), 0))
            self._polygon = QPolygonF(points)
        return self._polygon

    def draw(self):
        logger.debug("Drawing %s", self)

        # Calculate painter scaling factors.
        # Scale X so that distance scales to width() - 40:
        # Scale Y so that elevation diff maps to height() - 40. Y factor
        # is negative so y will actually grow "up".
        xscale = float(self.xaxis().scale()
                       if self.xaxis() and hasattr(self.xaxis(), "scale") and callable(self.xaxis().scale)
                       else self._graph.width() - 40)
        yscale = (float(self.scale()) + 
                  (2 * self.padding() if self.offset() != 0 else self.padding()) * float(self.scale()))
        logger.debug("%s.draw() xscale %s yscale %s", self, xscale, yscale)
        if xscale != 0 and yscale != 0:
            self._graph.painter.scale(
              (self._graph.width() - 40) / xscale,
              - (self._graph.height() - 40) / yscale)

            p = QPen(QColor(self.color()))
            p.setStyle(self.style())
            p.setWidth(0)
            self._graph.painter.setPen(p)
            if self.shade() is not None:
                self._graph.painter.setBrush(QColor(self.shade()))
                self._graph.painter.drawPolygon(self.polygon())
            else:
                self._graph.painter.drawPolyline(self.polygon())

            for trendline in self.trendLines():
                p = QPen(self.color())
                p.setWidth(0)
                p.setStyle(trendline.style())
                self._graph.painter.setPen(p)
                self._graph.painter.drawPolyline(trendline.polygon())

    def drawAxis(self, ix):
        pass

    def addTrendLine(self, formula, style=None):
        trendline = (formula
                     if isinstance(formula, Series)
                     else Series(value=formula, style=style or Qt.DashDotLine))
        trendline.setAxis(self)
        self._trendlines.append(trendline)

    def trendLines(self):
        return self._trendlines


class Graph(QWidget):
    def __init__(self, parent, ds, **kwargs):
        super(Graph, self).__init__(parent)
        self.setDatasource(ds)
        self._series = []
        self.setMinimumSize(350, 300)
        self.update()

    def paintEvent(self, pevent):
        self.draw()

    def setXAxis(self, xaxis):
        self._xaxis = xaxis
        if self._xaxis:
            self._xaxis.setGraph(self)

    def xaxis(self):
        return self._xaxis

    def datasource(self):
        return self._ds

    def setDatasource(self, ds):
        self._ds = ds
        self.setXAxis(ds if ds and hasattr(ds, "value") else None)
        
    def addSeries(self, series):
        self._series.append(series)
        self._series.sort(key=lambda s: s.layer())
        series.setGraph(self)

    def series(self):
        return self._series

    def xscale(self):
        return float(self._axis.scale())

    def draw(self):
        self.painter = QPainter(self)
        self.painter.setRenderHint(QPainter.Antialiasing)

        # Set origin to lower left hand corner:
        self.painter.translate(20, self.height() - 20)

        p = QPen(Qt.darkGray)
        p.setStyle(Qt.SolidLine)
        self.painter.setPen(p)
        w = self.width() - 40
        self.painter.drawLine(0, 0, 0, -self.height() - 40)
        self.painter.drawLine(0, 0, w, 0)

        if self.xaxis():
            fm = self.painter.fontMetrics()
            for ix in range(0, 5):
                v = str(self.xaxis().fromordinal(self.xaxis().offset() + (ix * self.xaxis().scale())/4))
                tw = fm.width(v)
                if ix == 0:
                    x = 0
                elif ix < 4:
                    x = (ix*w)/4 - tw/2
                else: # ix == 4
                    x = w - tw
                self.painter.drawText(x, fm.height(), v)
                if ix > 0:
                    self.painter.drawLine((ix*w)/4, 0, (ix*w)/4, - fm.height() / 2)

        for ix in range(0, len(self._series)):
            s = self._series[ix]
            if s.visible():
                self.painter.save()
                s.drawAxis(-(ix/2) if (ix % 2) == 0 else (ix-1)/2)
                s.draw()
                self.painter.restore()
        self.painter = None


if __name__ == "__main__":
    import random
    import sys
    from PyQt5.QtWidgets import QApplication
    
    class Point(object):
        def __init__(self, generator, ix):
            self.ix = ix
            self.hr = random.uniform(120, 180)
            generator.max_heartrate = max(self.hr, generator.max_heartrate)
            self.power = random.uniform(80, 400)
            generator.max_power = max(self.power, generator.max_power)
            self.elevation = random.uniform(generator.min_elev, generator.max_elev)
            self.corrected_elevation = self.elevation + random.uniform(-5, 5)
    
    class Generator(DataSource, Axis):
        def __init__(self, points):
            super(Generator, self).__init__(property="ix",
                                            name="Generator",
                                            offset=0)
            self.min_elev = 300
            self.max_elev = 400
            self.max_heartrate = None
            self.max_power = None
            self._points = [Point(self, ix) for ix in range(0, points)]
            
        def fetch(self):
            return self._points

    class GraphTest(QWidget):
        def __init__(self):
            super(GraphTest, self).__init__()

            x, y, w, h = 500, 200, 370, 320
            self.setGeometry(x, y, w, h)

            generator = Generator(100)
            self.graphs = Graph(self, generator)
            self.graphs.addSeries(Series(
                    max=generator.max_heartrate,
                    name="Heartrate",
                    property="hr",
                    color=Qt.red))
            p = Series(
                    graph=self.graphs,
                    max=generator.max_power,
                    name="Power",
                    property="power",
                    smooth=10,
                    color=Qt.blue)
            self.graphs.addSeries(Series(
                min=generator.min_elev,
                max=generator.max_elev,
                value=(lambda wp:
                       wp.corrected_elevation
                       if wp.corrected_elevation is not None
                       else wp.elevation if wp.elevation else 0),
                name="elevation",
                smooth=3,
                layer=0,
                color="peru",
                shade="sandybrown"))

        def show_and_raise(self):
            self.show()
            self.raise_()

    app = QApplication(sys.argv)

    demo = GraphTest()
    demo.show_and_raise()

    sys.exit(app.exec_())
