/*
 * Copyright (c) 2017 Jan de Visser (jan@sweattrails.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

com.sweattrails.app.session = {};

/* ----------------------------------------------------------------------- */

com.sweattrails.app.session.durationTicks = function() {
    var ticks = [];
    com.sweattrails.app.session.intervals.forEach(function(i) {
            ticks.push({label: format_elapsed_time(seconds_to_timeobj(i.timestamp)), value: i.timestamp});
        }, this);
    return ticks;
};

com.sweattrails.app.session.lapPaceTicks = function() {
    var ticks = [];
    com.sweattrails.app.session.intervals.forEach(function(i) {
            ticks.push({label: com.sweattrails.app.unitsystem.pace(i.average_speed, false), value: i.average_speed});
        }, this);
    return ticks;
};

com.sweattrails.app.session.paceTicks = function() {
    return [
        {label: com.sweattrails.app.unitsystem.pace(this.min, false), value: this.min},
        {
            label: com.sweattrails.app.unitsystem.pace(com.sweattrails.app.session.activity.average_speed, false),
            value: com.sweattrails.app.session.activity.average_speed
        },
        {label: com.sweattrails.app.unitsystem.pace(this.max, false), value: this.max}
    ];
};

com.sweattrails.app.session.powerTicks = function() {
    var ret = [ this.min, com.sweattrails.app.session.activity.average_power, this.max ];
    if (com.sweattrails.app.session.activity.normalized_power) {
        ret.push(com.sweattrails.app.session.normalized_power);
    }
    return ret;
};

com.sweattrails.app.session.elevationTicks = function() {
    return [ this.min.toFixed(0), this.max.toFixed(0) ];
};

com.sweattrails.app.session.timestampTicks = function() {
    var grid;
    if (this.max <= 600) {
        grid = 60;
    } else if (this.max <= 3600) {
        grid = 300;
    } else if (this.max <= 7200) {
        grid = 600;
    } else if (this.max <= 21600) {
        grid = 1800;
    } else {
        grid = 3600;
    }
    var ticks = [];
    for (var t = 0; t < this.max; t += grid) {
        ticks.push({label: format_elapsed_time(seconds_to_timeobj(t)), value: t});
    }
    ticks.push({label: format_elapsed_time(seconds_to_timeobj(this.max)), value: this.max});
    return ticks;
};
