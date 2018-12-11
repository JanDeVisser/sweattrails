'''
Created on 2013-03-10

@author: jan
'''

import os
import smtplib
import mimetypes
import email
import email.mime.multipart
import email.mime.base
import email.mime.text
import email.mime.audio
import email.mime.image
from email.encoders import encode_base64
import jinja2

import gripe

logger = gripe.get_logger(__name__)


def sendMail(recipients, subject, text, *attachmentFilePaths, **headers):
    message = email.mime.multipart.MIMEMultipart()
    message['From'] = gripe.Config.smtp.username
    message['To'] = recipients if isinstance(recipients, str) else ",".join(recipients)
    message['Subject'] = subject
    for header in headers:
        message[header] = headers[header]
    message.attach(email.mime.text.MIMEText(text))
    for attachmentFilePath in attachmentFilePaths:
        message.attach(getAttachment(attachmentFilePath))
    mailServer = smtplib.SMTP(gripe.Config.smtp.smtphost, gripe.Config.smtp.smtpport)
    mailServer.ehlo()
    mailServer.starttls()
    mailServer.ehlo()
    mailServer.login(gripe.Config.smtp.username, gripe.Config.smtp.password)
    mailServer.sendmail("%s <%s>" % (gripe.Config.smtp.fromname, gripe.Config.smtp.username), recipients, message.as_string())
    mailServer.close()
    logger.info('Sent email to %s', recipients)


def getAttachment(attachmentFilePath):
    logger.debug("Attaching file %s", attachmentFilePath)
    contentType, encoding = mimetypes.guess_type(attachmentFilePath)
    if contentType is None or encoding is not None:
        contentType = 'application/octet-stream'
    mainType, subType = contentType.split('/', 1)
    logger.debug("Mime %s main %s sub %s", contentType, mainType, subType)
    with open(attachmentFilePath, 'rb') as file:
        if mainType == 'text':
            attachment = email.mime.text.MIMEText(file.read())
        elif mainType == 'message':
            attachment = email.message_from_file(file)
        elif mainType == 'image':
            attachment = email.mime.image.MIMEImage(file.read(), _subType = subType)
        elif mainType == 'audio':
            attachment = email.mime.audio.MIMEAudio(file.read(), _subType = subType)
        else:
            attachment = email.mime.base.MIMEBase(mainType, subType)
            attachment.set_payload(file.read())
        logger.debug("attachment is a %s", attachment.__class__.__name__)
        encode_base64(attachment)
    attachment.add_header('Content-Disposition', 'attachment', filename = os.path.basename(attachmentFilePath))
    return attachment


class MailMessage(object):
    def __init__(self, ctx_factory = None):
        self._attachments = []
        self._headers = {}
        self._recipients = None
        self._subject = None
        self._body = ""
        self._ctx_factory = None
        self.context_factory(ctx_factory)

    def context_factory(self, ctx_factory = None):
        if ctx_factory is not None:
            self._ctx_factory = ctx_factory
        return self._ctx_factory if self._ctx_factory is not None else {}

    def recipients(self, recipients = None):
        if recipients is not None:
            recipients = recipients if not isinstance(recipients, str) else [ recipients ]
            self._recipients = recipients
        return self._recipients

    def recipients_string(self):
        return ",".join(self._recipients) if self._recipients else ""

    def subject(self, subject = None):
        if subject is not None:
            self._subject = subject
        return self._subject
    
    def body(self, body = None):
        if body is not None:
            self._body = body
        return self._body

    def set_header(self, header, value):
        self._headers[header] = value
        
    def headers(self):
        return self._headers
        
    def add_attachment(self, att_path):
        self._attachments.append(att_path)

    def attachments(self):
        return self._attachments

    def _get_template(self):
        return jinja2.Template(self.body())

    def send(self, ctx = None):
        recipients = self.recipients if not isinstance(self.recipients, str) else [ recipients ]
        ctx_factory = self.context_factory()
        if ctx_factory is not None:
            if callable(ctx_factory):
                ctx = ctx_factory(self, ctx)
            elif ctx is not None:
                ctx.update(ctx_factory)
        ctx = ctx if ctx is not None else {}
        if "app" not in ctx:
            ctx['app'] = gripe.Config.app.get("about", {})
        if "config" not in ctx:
            ctx['config'] = gripe.Config.app.get("config", {})
        if "msg" not in ctx:
            ctx["msg"] = self
        if hasattr(self, "get_context") and callable(self.get_context):
            ctx = self.get_context(ctx)
        ctx = ctx if ctx is not None else {}
        return sendMail(self.recipients(), self.subject(), 
            self._get_template().render(ctx), *self.attachments(), **self.headers())


class TemplateMailMessage(MailMessage):
    template_dir = "mailtemplate"

    def __init__(self, template = None, ctx_factory = None):
        self._template = None
        self.template(template)
        super(TemplateMailMessage, self).__init__(ctx_factory)

    def template(self, template = None):
        if template is not None:
            self._template = template
        return self._template

    @classmethod
    def _get_env(cls):
        if not hasattr(cls, "env"):
            loader = jinja2.ChoiceLoader([ \
                jinja2.FileSystemLoader("%s/%s" % (gripe.root_dir(), cls.template_dir)), \
                jinja2.PackageLoader("gripe", "template") \
            ])
            env = jinja2.Environment(loader = loader)
            if hasattr(cls, "get_env") and callable(cls.get_env):
                env = cls.get_env(env)
            cls.env = env
        return cls.env

    def _get_template(self):
        tpl = self.template()
        if not tpl:
            tpl = self.get_template() \
                if hasattr(self, "get_template") and callable(self.get_template) \
                else None
        cname = self.__class__.__name__.lower()
        if not tpl:
            tpl = cname
        tpl = gripe.Config.app.get(cname, tpl)
        logger.info("TemplateMailMessage: using template %s", tpl)
        return self._get_env().get_template(tpl + ".txt")


if __name__ == '__main__':
    sendMail("jan@de-visser.net", "Test", """
Hi Jan,
    
This is a test.

Thanks,

jan
""", *[ "%s/image/Desert.jpg" % gripe.root_dir() ])


    message = TemplateMailMessage("testmessage")
    message.recipients("jan@de-visser.net")
    message.subject("Test")
    message.send()
