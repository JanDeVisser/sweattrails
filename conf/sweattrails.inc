{
    "author": "Jan de Visser",
    "version": "1.0",
    "mounts": [
        { "path": "/jscr/sweattrails/.+", "roles": [], "relpath": "/sweattrails/js" },
        { "path": "/st/.+",               "app": "sweattrails.web.handlers.app", "roles": [ "athlete" ]},
        { "path": "/weight",              "app": "sweattrails.web.app", "roles": [ "user" ]}
    ],
    "roles": {
        "athlete": {
            "urls": [ { "id": "activities", "label": "Activities", "url": "/st/activities", "level": 20 } ],
            "label": "Athlete",
            "has_roles": [ "user" ]
        },
        "coach": {
              "label": "Coach",
              "has_roles": [ "user" ]
        },
        "admin": {
        }
    },

    "grizzle": {
        "userparts": {
            "sweattrails.userprofile.UserProfile": { "default": true, "configurable": false },
            "sweattrails.config.ActivityProfile": { "default": true, "configurable": false },
            "sweattrails.userprofile.WeightMgmt": {
                "default": true, "configurable": true,
                "label": "Weight Tracking",
                "urls": [
                    { "id": "weighttracking", "label": "Weight Tracking", "url": "/weight", "level": 10 }
                ]
            }
        }
    },
    "sweattrails": {
        "activityprofileparts": {
            "sessionType": {
                "nodeClass": "sweattrails.config.SessionTypeNode",
                "refClass": "sweattrails.config.SessionType"
            },
            "gearType": {
                "nodeClass": "sweattrails.config.GearTypeNode",
                "refClass": "sweattrails.config.GearType"
            },
            "criticalPowerInterval": {
                "nodeClass": "sweattrails.config.CriticalPowerIntervalNode",
                "refClass": "sweattrails.config.CriticalPowerInterval"
            },
            "criticalPace": {
                "nodeClass": "sweattrails.config.CriticalPaceNode",
                "refClass": "sweattrails.config.CriticalPace"
            },
            "paceZone": {
                "nodeClass": "sweattrails.config.PaceZoneNode",
                "refClass": "sweattrails.config.PaceZone"
            },
            "powerZone": {
                "nodeClass": "sweattrails.config.PowerZoneNode",
                "refClass": "sweattrails.config.PowerZone"
            },
            "heartrateZone": {
                "nodeClass": "sweattrails.config.HeartrateZoneNode",
                "refClass": "sweattrails.config.HeartrateZone"
            }
        },
        "background": {
            "plugins": [
                "sweattrails.qt.imports.ScanInbox"
            ],
            "_plugins": [
                "sweattrails.qt.bg.webserver.WebServer"
            ],
            "webserver": {
                "handlers": { "path": "/strava/auth", "handler": "sweattrails.strava.Handler"}
            }
        }
    }
}
