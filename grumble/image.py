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


import hashlib
import os
# imp psycopg2
import gripe
import gripe.db
import grumble.converter
import grumble.property


# class BinaryConverter(grumble.converter.PropertyConverter):
#     datatype = psycopg2.Binary
#
#     def convert(self, value):
#         # psycopg2.Binary is a function, not a class. Therefore our isinstance
#         # test in the base convert method doesn't work.
#         return self.datatype(value)


class BinaryProperty(grumble.property.ModelProperty):
    datatype = bytes
    sqltype = "BYTEA"

    def _to_json_value(self, instance, value):
        raise gripe.NotSerializableError(self.name)

    def _from_json_value(self, value):
        raise gripe.NotSerializableError(self.name)


class ImageProperty(grumble.property.CompoundProperty):
    def __init__(self, **kwargs):
        bin_kwargs = {"suffix": "_blob"}
        ct_kwargs = {"suffix": "_ct"}
        hash_kwargs = {"suffix": "_hash"}
        if "verbose_name" in kwargs:
            bin_kwargs["verbose_name"] = kwargs["verbose_name"]
        super(ImageProperty, self).__init__(
            BinaryProperty(**bin_kwargs),
            grumble.property.StringProperty(**ct_kwargs),
            grumble.property.StringProperty(**hash_kwargs),
            **kwargs
        )

    def __set__(self, instance, value):
        if isinstance(value, tuple) or isinstance(value, list):
            assert len(value) in [2, 3], "Cannot assign sequence of length %s to ImageProperty" % len
            if len(value) == 3:
                v = value
            else:
                v = (value[0], value[1], hashlib.md5(value[0]).hexdigest())
        else:
            assert isinstance(value, str), "Can't assign %s (%s) to ImageProperty" % (value, type(value))
            with open(value, "rb") as fh:
                content = fh.read()
            ct = gripe.ContentType.for_path(value)
            v = (content, ct.content_type if ct else None, hashlib.md5(content).hexdigest())
        super(ImageProperty, self).__set__(instance, v)


if __name__ == "__main__":
    import grumble.model

    class Test(grumble.model.Model):
        label = grumble.property.TextProperty(required=True)
        image = ImageProperty()

    with gripe.db.Tx.begin():
        with open("C:/Users/Public/Pictures/Sample Pictures/Desert.jpg", "rb") as desert_fh:
            img = desert_fh.read()
            desert = Test(label="Desert")
            desert.image = (img, "image/jpeg")
            desert.put()
            k = desert.key()

    try:
        os.remove("C:/Users/Public/Pictures/Sample Pictures/Desert_1.jpg")
    except IOError:
        pass

    with gripe.db.Tx.begin():
        desert = Test.get(k)
        with open("C:/Users/Public/Pictures/Sample Pictures/Desert_1.jpg", "wb") as desert_fh:
            desert_fh.write(desert.image_blob)
