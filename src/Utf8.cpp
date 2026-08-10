/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <fstream>
#include <iconv.h>
#include <print>

#include <Utf8.h>

namespace ST {

struct UTF8_std {
    UTF8_std()
        : converter { std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(locale) }
    {
    }

    static UTF8_std utf8;

    std::locale                                   locale;
    std::codecvt<wchar_t, char, mbstate_t> const &converter;

    Result<std::string> to_utf8(std::wstring_view const &s)
    {
        std::mbstate_t mb { }; // initial shift state
        std::string    ret(s.size() * converter.max_length(), '\0');
        wchar_t const *from_next;
        char          *to_next;
        if (auto res = converter.out(mb, &s[0], &s[s.size()], from_next,
                &ret[0], &ret[ret.size()], to_next);
            res != std::codecvt_base::ok) {
            return LibCError(EINVAL);
        }
        ret.resize(to_next - &ret[0]);
        return ret;
    }

    Result<std::wstring> to_wstring(std::string_view const &s)
    {
        std::mbstate_t mb { }; // initial shift state
        std::wstring   ret(s.size(), '\0');
        char const    *from_next;
        wchar_t       *to_next;
        if (auto res = converter.in(mb, &s[0], &s[s.size()], from_next,
                &ret[0], &ret[ret.size()], to_next);
            res != std::codecvt_base::ok) {
            return LibCError(EINVAL);
        }
        ret.resize(to_next - &ret[0]);
        return ret;
    }

    Result<ssize_t> write(std::basic_ofstream<wchar_t> &os, std::wstring_view const &contents)
    {
        os.imbue(locale);
        os.write(contents.data(), contents.length());
        if (os.fail() || os.bad()) {
            return LibCError();
        }
        return contents.length();
    }

    Result<std::wstring> read(std::basic_ifstream<wchar_t> &is)
    {
        is.imbue(locale);
        std::wstring ret;
        for (wchar_t ch; is.get(ch);) {
            ret += ch;
        }
        std::println("Read file:");
        std::wcout << ret << std::endl;
        return ret;
    }
};

struct UTF8 {
    UTF8()
        : cd_in(iconv_open("UTF-32-INTERNAL", "UTF-8"))
        , cd_out(iconv_open("UTF-8", "UTF-32-INTERNAL"))
    {
        assert(cd_in != reinterpret_cast<iconv_t>(-1));
        assert(cd_out != reinterpret_cast<iconv_t>(-1));
    }

    static UTF8 utf8;

    iconv_t cd_in;
    iconv_t cd_out;

    Result<std::string> to_utf8(std::wstring_view const &s)
    {
        auto        data = const_cast<wchar_t *>(s.data());
        size_t      in_count = s.length() * sizeof(wchar_t);
        std::string ret;
        ret.resize(s.length() * 16, '\0');
        size_t out_count = s.length() * 16;
        auto   out_buffer = ret.data();
        if (auto done = iconv(cd_out,
                reinterpret_cast<char **>(&data), &in_count,
                &out_buffer, &out_count);
            static_cast<int>(done) < 0) {
            return LibCError();
        }
        ret.resize(s.length() * 16 - out_count);
        while (!ret.empty() && ret[ret.size() - 1] == 0) {
            ret.resize(ret.size() - 1); // HACK somehow iconv sometimes leaves
                                        // '0' chars at the end of the
                                        // converted string
        }
        return ret;
    }

    Result<std::wstring> to_wstring(std::string_view const &s)
    {
        auto         data = const_cast<char *>(s.data());
        size_t       in_count = s.length();
        std::wstring ret;
        ret.resize(2 * s.length(), '\0');
        size_t out_count = 2 * s.length() * sizeof(wchar_t);
        auto   out_buffer = ret.data();
        if (auto done = iconv(cd_in,
                &data, &in_count,
                reinterpret_cast<char **>(&out_buffer), &out_count);
            static_cast<int>(done) < 0) {
            return LibCError();
        }
        ret.resize(2 * s.length() * sizeof(wchar_t) - out_count);
        while (!ret.empty() && ret[ret.size() - 1] == 0) {
            ret.resize(ret.size() - 1);
        }
        return ret;
    }

    Result<ssize_t> write(std::ofstream &os, std::wstring_view const &contents)
    {
        // TODO stream instead of allocating whole new string but RAM is cheap
        // so whatever
        auto converted = TRY_EVAL(to_utf8(contents));
        os << converted;
        if (os.fail() || os.bad()) {
            return LibCError();
        }
        return contents.length();
    }

    Result<std::wstring> read(std::ifstream &is)
    {
        std::string contents;
        for (char ch; is.get(ch);) {
            contents += ch;
        }
        auto ret = TRY_EVAL(to_wstring(contents));
        return ret;
    }
};

UTF8 UTF8::utf8 { };

Result<std::string> to_utf8(std::wstring_view const &s)
{
    return UTF8::utf8.to_utf8(s);
}

Result<std::wstring> to_wstring(std::string_view const &s)
{
    return UTF8::utf8.to_wstring(s);
}

Result<ssize_t> write_utf8(std::ofstream &os, std::wstring_view const &contents)
{
    return UTF8::utf8.write(os, contents);
}

Result<std::wstring> read_utf8(std::ifstream &is)
{
    return UTF8::utf8.read(is);
}

}
