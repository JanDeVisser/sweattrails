# To change this license header, choose License Headers in Project Properties.
# To change this template file, choose Tools | Templates
# and open the template in the editor.

__author__="jan"
__date__ ="$24-Nov-2013 12:20:43 PM$"

import grudge
import grumble
import sweattrails.config

class Manufacturer(grumble.Model):
    name = grumble.TextProperty(is_key = True, is_label = True)
    country = grumble.ReferenceProperty(sweattrails.config.Country)
    notes = grumble.TextProperty(multiline = True)

class Medication(grumble.Model):
    medication = grumble.TextProperty(is_key = True, is_label = True)
    manufacturer = grumble.ReferenceProperty(Manufacturer)
    description = grumble.TextProperty()
    adverse_effects = grumble.TextProperty()

class MedicationHistory(grumble.Model):
    timestamp = grumble.DateProperty(auto_now_add = True)
    current = grumble.BooleanProperty()
    medication = grumble.ReferenceProperty(Medication)
    dose = grumble.TextProperty()
    units = grumble.TextProperty("mg", "tablets", "g", "Other")
    frequency = grumble.TextProperty(choices = ["Daily", "Twice a day", "Three times a day", "Four times a day", "Other"])
    experiences = grumble.TextProperty(multiline = True)

#
# ==========================================================================
#  N E W  M E D I C A T I O N  R E Q U E S T S
# ==========================================================================
#

@grudge.OnStarted("create_user")
@grudge.OnAdd("user_exists", grudge.Stop())
@grudge.OnAdd("user_created", grudge.SendMail(recipients = "@.:userid",
    subject = "Confirm your registration with %s" % gripe.Config.app.about.application_name,
    text = "&signup_confirmation", status = "mail_sent", context = ".:prepare_message"))
@grudge.OnAdd("confirmed", "activate_user")
@grudge.OnAdd("user_activated", grudge.Stop())
@grudge.Process()
class MedicationRequest(grumble.Model):
    medication = grumble.TextProperty(is_label = True)
    manufacturer = grumble.TextProperty()
    remarks = grumble.TextProperty()

    medication_exists = grudge.Status()
    medication_created = grudge.Status()
    confirmed = grudge.Status()
    user_activated = grudge.Status()

    def create_user(self):
        try:
            um = grit.Session.get_usermanager()
            um.add(self.userid, self.password, self.display_name)
            logger.debug("Create User OK")
            return self.user_created
        except gripe.auth.UserExists as e:
            return self.user_exists
        except gripe.Error as e:
            logger.debug("Create user Error: %s" % e)
            raise

    def prepare_message(self, msg, ctx):
        msg.set_header("X-ST-URL", "http://localhost/um/confirm/%s" % self.id())
        return ctx

    def activate_user(self):
        um = grit.Session.get_usermanager()
        try:
            um.confirm(self.userid)
            logger.debug("Activate User OK")
            return self.user_activated
        except gripe.Error as e:
            logger.debug("Activate user Error: %s" % e)
            raise

