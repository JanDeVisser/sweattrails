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

com.sweattrails.app = {};

com.sweattrails.app.native_unit_system = "metric";
com.sweattrails.app._current_unitsystem = null;

Object.defineProperty(com.sweattrails.app, 'current_unitsystem', {
    get: function() {
        return this._current_unitsystem;
    },
    set: function(unitsystem) {
        this._unitsystem = null;
        this._current_unitsystem = unitsystem;
    }
});

com.sweattrails.app.current_unitsystem = com.sweattrails.app.native_unit_system;

com.sweattrails.app.unitsystems = {};
com.sweattrails.app._unitsystem = null;

Object.defineProperty(com.sweattrails.app, 'unitsystem', {
    get: function() {
        if (!this._unitsystem) {
            console.log("current_unitsystem: %s", com.sweattrails.app.current_unitsystem);
            com.sweattrails.app._unitsystem = com.sweattrails.app.unitsystems[com.sweattrails.app.current_unitsystem];
            console.log("_unit_system: %s", com.sweattrails.app._unitsystem.id);
        }
        return this._unitsystem;
    },
    enumerable: true,
    configurable: true
});

com.sweattrails.app.BaseUnitSystem = function() {
};

com.sweattrails.app.BaseUnitSystem.prototype.unit = function(which) {
    return this.units_table[which].label;
};

com.sweattrails.app.BaseUnitSystem.prototype.speed = function(spd_ms, include_unit) {
    if (arguments.length < 2) {
        include_unit = true;
    }
    var spd = this.native_speed(spd_ms);
    var ret = spd.toFixed(2);
    if (include_unit) {
	    ret += " " + this.unit('speed');
    }
    return ret;
};

com.sweattrails.app.BaseUnitSystem.prototype.avgspeed = function(distance, t, include_unit) {
    if (arguments.length < 3) {
        include_unit = true;
    }
    var seconds = 3600 * t.hour + 60 * t.minute + t.second;
    return this.speed(distance / seconds, include_unit);
};

com.sweattrails.app.BaseUnitSystem.prototype.pace_as_number = function(speed_ms) {
    var spd = this.native_speed(speed_ms, false);
    var p = 60 / spd;
    return p.toFixed(2);
};

com.sweattrails.app.BaseUnitSystem.prototype.pace = function(speed_ms, include_unit) {
    if (arguments.length < 2) {
        include_unit = true;
    }
    var p = this.pace_as_number(speed_ms);
    var pmin = Math.floor(p);
    var psec = Math.floor((p - pmin) * 60);
    var ret = pmin + ":";
    if (psec < 10) {
	    ret += "0";
    }
    ret += psec;
    if (include_unit) {
	    ret += " " + this.unit("pace");
    }
    return ret;
};

com.sweattrails.app.BaseUnitSystem.prototype.avgpace = function(distance, t, include_unit) {
    if (arguments.length < 3) {
        include_unit = true;
    }
    var seconds = 3600*t.hour + 60*t.minute + t.second;
    return this.pace(distance/seconds, include_unit);
};

com.sweattrails.app.BaseUnitSystem.prototype.convert = function(metric_value, what, include_unit, digits) {
    if (arguments.length < 4) {
        digits = 2;
    }
    if (arguments.length < 3) {
        include_unit = true;
    }
    var ret = (metric_value * this.units_table[what].factor).toFixed(digits);
    if (include_unit) {
	    ret += " " + this.unit(what);
    }
    return ret;
};

com.sweattrails.app.BaseUnitSystem.prototype.to_metric = function(value, what) {
    return (value) ? parseFloat(value) / this.units_table[what].factor : 0.0;
};

com.sweattrails.app.BaseUnitSystem.prototype.length = function(len_in_cm, include_unit) {
    if (arguments.length < 2) {
        include_unit = true;
    }
    return this.convert(parseFloat(len_in_cm), 'length', include_unit, 0);
};

com.sweattrails.app.BaseUnitSystem.prototype.weight = function(weight_in_kg, include_unit) {
    if (arguments.length < 2) {
        include_unit = true;
    }
    return this.convert(parseFloat(weight_in_kg), 'weight', include_unit, 0);
};

com.sweattrails.app.BaseUnitSystem.prototype.distance = function(distance_in_km, include_unit) {
    if (arguments.length < 2) {
        include_unit = true;
    }
    return this.convert(parseFloat(distance_in_km), 'distance', include_unit, 2);
};

/* ----------------------------------------------------------------------- */

com.sweattrails.app.MetricSystem = function() {
    this.type = "unitsystem";
    this.id = "metric";
    $$.register(this);
    this.units_table = {
        distance: { label: 'km',     factor: 1.0    },
        speed:    { label: 'km/h',   factor: 1.0    },
        length:   { label: 'cm',     factor: 1.0    },
        weight:   { label: 'kg',     factor: 1.0    },
        pace:     { label: 'min/km',                },
        height:   { label: 'm'                      }
    };
    com.sweattrails.app.unitsystems.metric = com.sweattrails.app.unitsystems.m = this;
};

com.sweattrails.app.MetricSystem.prototype = new com.sweattrails.app.BaseUnitSystem();

com.sweattrails.app.MetricSystem.prototype.format_distance = function(meters) {
    if (meters < 1000) {
        return meters + " m";
    } else {
        var km = parseFloat(meters) / 1000.0;
        if (km < 10) {
            return km.toFixed(3) + " km";
        } else if (km < 100) {
            return km.toFixed(2) + " km";
        } else {
            return km.toFixed(1) + " km";
        }
    }
};

com.sweattrails.app.MetricSystem.prototype.native_speed = function (mps) {
    return mps * 3.6;
};

com.sweattrails.app.MetricSystem.prototype.height = function (height_in_cm, include_unit) {
    if (arguments.length < 2) {
        include_unit = true;
    }
    height_in_cm = parseFloat(height_in_cm);
    var h = (height_in_cm / 100).toFixed(2);
    if (include_unit) {
        h += ' ' + self.unit('height');
    }
    return h;
};

/* ----------------------------------------------------------------------- */

com.sweattrails.app.ImperialSystem = function() {
    this.type = "unitsystem";
    this.id = "imperial";
    $$.register(this);
    this.units_table = {
        distance: { label: 'mile',     factor: 0.621371192    },
        speed:    { label: 'mph',      factor: 0.621371192    },
        length:   { label: 'in',       factor: 0.393700787    },
        weight:   { label: 'lbs',      factor: 2.20462262     },
        pace:     { label: 'min/mile'                         },
        height:   { label: 'ft/in'                            }
    };
    com.sweattrails.app.unitsystems.imperial = com.sweattrails.app.unitsystems.i = this;
};

com.sweattrails.app.ImperialSystem.prototype = new com.sweattrails.app.BaseUnitSystem();

com.sweattrails.app.ImperialSystem.prototype.format_distance = function(meters) {
    var miles = meters * 0.0006213712;
    if (miles < 100) {
        return miles.toFixed(3) + " mi";
    } else {
        return miles.toFixed(2) + " mi";
    }
};

com.sweattrails.app.ImperialSystem.prototype.native_speed = function (spd_ms) {
    return (spd_ms * 3.6) /* km/h */ * 0.6213712;
};

com.sweattrails.app.ImperialSystem.prototype.height = function (height_in_cm, include_unit) {
    if (arguments.length < 2) {
        include_unit = true;
    }
    height_in_cm = parseFloat(height_in_cm);
    var h_in = Math.round(height_in_cm * 0.393700787);
    var ft = Math.floor(h_in / 12);
    var inches = h_in % 12;
    var ret = '';
    if (ft > 0) {
        ret = ft + ((include_unit) ? "' " : " ");
    }
    ret += inches + ((include_unit) ? '" ' : " ");
    return ret;
};

/* ----------------------------------------------------------------------- */

new com.sweattrails.app.MetricSystem();
new com.sweattrails.app.ImperialSystem();

/* ----------------------------------------------------------------------- */

/*
 * Table column type support
 */

com.sweattrails.api.internal.format.distance = function(value) {
    return com.sweattrails.app.unitsystem.format_distance(value);
};

com.sweattrails.api.internal.format.weight = function(value) {
    return com.sweattrails.app.unitsystem.weight(value, true);
};

com.sweattrails.api.internal.format.length = function(value) {
    return com.sweattrails.app.unitsystem.length(value, true);
};

com.sweattrails.api.internal.format.pace = function(value) {
    return com.sweattrails.app.unitsystem.pace(value, true);
};

/*
 * Form field type support
 */

/*
 * WeightField -
 */

com.sweattrails.api.WeightField = function(fld, elem) {
    this.type = "number";
    this.field = fld;
};

com.sweattrails.api.WeightField.prototype.renderEdit = function(value) {
    this.span = document.createElement("span");
    this.span = this.field.getElementID("container");
    this.control = document.createElement("input");
    var w = null;
    if (value) {
        w = weight(parseFloat(value), native_unit, false);
    }
    this.control.value = w || "";
    this.control.type = "text";
    this.control.maxLength = 6;
    this.control.size = 4; // WAG
    this.control.onchange = this.field.onValueChange.bind(this.field);
    this.control.oninput = this.field.onInput.bind(this.field);
    this.control.id = this.field.getElementID();
    this.control.name = this.id;
    this.span.appendChild(this.control);
    this.unitSelector = document.createElement("select");
    this.unitSelector.id = this.field.getElementID("units");
    this.unitSelector.name = this.field.id + "-units";
    this.nativeUnitIndex = (native_unit === "m") ? 0 : 1;
    var option = document.createElement("option");
    option.selected = (native_unit === "m");
    option.value = "1.0";
    option.text = "kg";
    this.unitSelector.appendChild(option);
    option = document.createElement("option");
    option.selected = (native_unit === "i");
    option.value = "2.20462262";
    option.text = "lbs";
    this.unitSelector.onchange = this.field.onValueChange.bind(this.field);
    this.unitSelector.appendChild(option);
    this.span.appendChild(this.unitSelector);
    return this.span;
};

com.sweattrails.api.WeightField.prototype.setValueFromControl = function(bridge, object) {
    this.value = parseFloat(this.control.value) / parseFloat(this.unitSelector.value);
    bridge.setValue(object, this.value);
};

com.sweattrails.api.WeightField.prototype.renderView = function(value) {
    var ret = document.createElement("span");
    var w = null;
    if (value) {
        w = com.sweattrails.app.unitsystem.weight(parseFloat(value), native_unit, true);
    }
    ret.innerHTML = w || "";
    return ret;
};

com.sweattrails.api.WeightField.prototype.clear = function() {
    if (this.control) {
        this.control.value = "";
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

com.sweattrails.api.WeightField.prototype.setValue = function(value) {
    // FIXME: This assumes the value set is in the user's native unit. This is
    // probably wrong. It's probably in the system unit.
    if (this.control) {
        this.control.value = value;
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

/*
 * LengthField -
 */

com.sweattrails.api.LengthField = function(fld, elem) {
    this.type = "number";
    this.field = fld;
};

com.sweattrails.api.LengthField.prototype.renderEdit = function(value) {
    this.span = document.createElement("span");
    this.span.id = this.field.getElementID("-container");
    this.control = document.createElement("input");
    var l = null;
    if (value) {
        l = com.sweattrails.app.unitsystem.length(parseFloat(value), false);
    }
    this.control.value = l || "";
    this.control.name = this.field.id;
    this.control.id = this.field.id;
    this.control.type = "text";
    this.control.maxLength = 6;
    this.control.size = 4; // WAG
    this.control.onchange = this.field.onValueChange.bind(this.field);
    this.control.oninput = this.field.onInput.bind(this.field);
    this.control.id = this.field.getElementID();
    this.control.name = this.id;
    this.span.appendChild(this.control);
    this.nativeUnitIndex = (com.sweattrails.app.current_unitsystem === "metric") ? 0 : 1;
    this.unitSelector = document.createElement("select");
    this.unitSelector.name = this.field.id + "-units";
    this.unitSelector.id = this.field.getElementID("-units");
    var option = document.createElement("option");
    option.selected = (com.sweattrails.app.current_unitsystem === "metric");
    option.value = "1.0";
    option.text = "cm";
    this.unitSelector.appendChild(option);
    option = document.createElement("option");
    option.selected = (com.sweattrails.app.current_unitsystem !== "metric");
    option.value = "0.393700787";
    option.text = "ft/in";
    this.unitSelector.appendChild(option);
    this.unitSelector.onchange = this.field.onValueChange.bind(this.field);
    this.span.appendChild(this.unitSelector);
    return this.span;
};

com.sweattrails.api.LengthField.prototype.setValueFromControl = function(bridge, object) {
    this.value = parseFloat(this.control.value) / parseFloat(this.unitSelector.value);
    var v = this.control.value;
    if (v) {
        v = v.trim();
        if ((this.unitSelector.value !== 1.0) && (v.indexOf("'") > 0)) {
            var a = v.split("'");
            v = 12*parseInt(a[0].trim()) + parseInt(a[1].trim());
        } else {
            v = parseFloat(v);
        }
        this.value = v / parseFloat(this.unitSelector.value);
    } else {
        this.value = 0;
    }
    bridge.setValue(object, this.value);
};

com.sweattrails.api.LengthField.prototype.renderView = function(value) {
    var ret = document.createElement("span");
    var l = null;
    if (value) {
        l = com.sweattrails.app.unitsystem.length(parseFloat(value), true);
    }
    ret.innerHTML = l || "";
    return ret;
};

com.sweattrails.api.LengthField.prototype.clear = function() {
    if (this.control) {
        this.control.value = "";
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

com.sweattrails.api.LengthField.prototype.setValue = function(value) {
    // FIXME: This assumes the value set is in the user's native unit. This is
    // probably wrong. It's probably in the system unit.
    if (this.control) {
        this.control.value = value;
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

/*
 * DistanceField -
 */

com.sweattrails.api.DistanceField = function(fld, elem) {
    this.type = "number";
    this.field = fld;
};

com.sweattrails.api.DistanceField.prototype.renderEdit = function(value) {
    this.span = document.createElement("span");
    this.span.id = this.field.getElementID("container");
    this.control = document.createElement("input");
    var l = null;
    if (value) {
        l = com.sweattrails.app.unitsystem.distance(parseFloat(value), native_unit, false);
    }
    this.control.value = l || "";
    this.control.name = this.field.id;
    this.control.id = this.field.getElementID();
    this.control.type = "text";
    this.control.maxLength = 6;
    this.control.size = 6; // WAG
    this.control.onchange = this.field.onValueChange.bind(this.field);
    this.control.oninput = this.field.onInput.bind(this.field);
    this.span.appendChild(this.control);
    this.nativeUnitIndex = (native_unit === "m") ? 0 : 1;
    this.unitSelector = document.createElement("select");
    this.unitSelector.id = this.field.getElementID("units");
    this.unitSelector.name = this.field.id + "-units";
    var option = document.createElement("option");
    option.selected = (native_unit === "m");
    option.value = "1000";
    option.text = "km";
    this.unitSelector.appendChild(option);
    option = document.createElement("option");
    option.selected = (native_unit === "i");
    option.value = "1608";
    option.text = "mile";
    this.unitSelector.appendChild(option);
    this.unitSelector.onchange = this.field.onValueChange.bind(this.field);
    this.span.appendChild(this.unitSelector);
    return this.span;
};

com.sweattrails.api.DistanceField.prototype.setValueFromControl = function(bridge, object) {
    this.value = parseFloat(this.control.value) / parseFloat(this.unitSelector.value);
    var v = this.control.value;
    if (v) {
        v = v.trim();
        this.value = parseFloat(v) * parseFloat(this.unitSelector.value);
    } else {
        this.value = 0;
    }
    bridge.setValue(object, this.value);
};

com.sweattrails.api.DistanceField.prototype.renderView = function(value) {
    var ret = document.createElement("span");
    var l = null;
    if (value) {
        l = com.sweattrails.app.unitsystem.distance(parseFloat(value) / 1000.0, true);
    }
    ret.innerHTML = l || "";
    return ret;
};

com.sweattrails.api.DistanceField.prototype.clear = function() {
    if (this.control) {
        this.control.value = "";
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

com.sweattrails.api.DistanceField.prototype.setValue = function(value) {
    // FIXME: This assumes the value set is in the user's native unit. This is
    // probably wrong. It's probably in the system unit.
    if (this.control) {
        this.control.value = value;
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

/* -- P A C E F I E L D -------------------------------------------------- */

com.sweattrails.api.PaceField = function(fld, elem) {
    this.type = "number";
    this.field = fld;
};

com.sweattrails.api.PaceField.prototype.renderEdit = function(value) {
    this.span = document.createElement("span");
    this.span.id = this.field.getElementID("container");
    this.control = document.createElement("input");
    var l = null;
    if (value) {
        l = com.sweattrails.app.unitsystem.pace(parseFloat(value), native_unit, false);
    }
    this.control.value = l || "";
    this.control.name = this.field.id;
    this.control.id = this.field.getElementID();
    this.control.type = "text";
    this.control.maxLength = 6;
    this.control.size = 6; // WAG
    this.control.onchange = this.field.onValueChange.bind(this.field);
    this.control.oninput = this.field.onInput.bind(this.field);
    this.span.appendChild(this.control);
    this.nativeUnitIndex = (native_unit === "m") ? 0 : 1;
    this.unitSelector = document.createElement("select");
    this.unitSelector.id = this.field.getElementID("units");
    this.unitSelector.name = this.field.id + "-units";
    var option = document.createElement("option");
    option.selected = (native_unit === "m");
    option.value = "1000";
    option.text = "km";
    this.unitSelector.appendChild(option);
    option = document.createElement("option");
    option.selected = (native_unit === "i");
    option.value = "1608";
    option.text = "mile";
    this.unitSelector.appendChild(option);
    this.unitSelector.onchange = this.field.onValueChange.bind(this.field);
    this.span.appendChild(this.unitSelector);
    return this.span;
};

com.sweattrails.api.PaceField.prototype.setValueFromControl = function(bridge, object) {
    this.value = parseFloat(this.control.value) / parseFloat(this.unitSelector.value);
    var v = this.control.value;
    if (v) {
        v = v.trim();
        this.value = parseFloat(v) * parseFloat(this.unitSelector.value);
    } else {
        this.value = 0;
    }
    bridge.setValue(object, this.value);
};

com.sweattrails.api.PaceField.prototype.renderView = function(value) {
    var ret = document.createElement("span");
    var l = null;
    if (value) {
        l = com.sweattrails.app.unitsystem.pace(parseFloat(value), true);
    }
    ret.innerHTML = l || "";
    return ret;
};

com.sweattrails.api.PaceField.prototype.clear = function() {
    if (this.control) {
        this.control.value = "";
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

com.sweattrails.api.PaceField.prototype.setValue = function(value) {
    // FIXME: This assumes the value set is in the user's native unit. This is
    // probably wrong. It's probably in the system unit.
    if (this.control) {
        this.control.value = value;
    }
    if (this.unitSelector) {
        this.unitSelector.selectedIndex = this.nativeUnitIndex;
    }
};

com.sweattrails.api.internal.fieldtypes.weight = com.sweattrails.api.WeightField;
com.sweattrails.api.internal.fieldtypes.length = com.sweattrails.api.LengthField;
com.sweattrails.api.internal.fieldtypes.distance = com.sweattrails.api.DistanceField;
com.sweattrails.api.internal.fieldtypes.pace = com.sweattrails.api.PaceField;
