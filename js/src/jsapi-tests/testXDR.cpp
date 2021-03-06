/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=99:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "tests.h"
#include "jsscript.h"
#include "jsstr.h"
#include "jsfriendapi.h"

static JSScript *
CompileScriptForPrincipalsVersionOrigin(JSContext *cx, JS::HandleObject obj,
                                        JSPrincipals *principals, JSPrincipals *originPrincipals,
                                        const char *bytes, size_t nbytes,
                                        const char *filename, unsigned lineno,
                                        JSVersion version)
{
    size_t nchars;
    if (!JS_DecodeBytes(cx, bytes, nbytes, NULL, &nchars))
        return NULL;
    jschar *chars = static_cast<jschar *>(JS_malloc(cx, nchars * sizeof(jschar)));
    if (!chars)
        return NULL;
    JS_ALWAYS_TRUE(JS_DecodeBytes(cx, bytes, nbytes, chars, &nchars));
    JS::CompileOptions options(cx);
    options.setPrincipals(principals)
           .setOriginPrincipals(originPrincipals)
           .setFileAndLine(filename, lineno)
           .setVersion(version);
    JSScript *script = JS::Compile(cx, obj, options, chars, nchars);
    free(chars);
    return script;
}

JSScript *
FreezeThaw(JSContext *cx, JSScript *script)
{
    // freeze
    uint32_t nbytes;
    void *memory = JS_EncodeScript(cx, script, &nbytes);
    if (!memory)
        return NULL;

    // thaw
    script = JS_DecodeScript(cx, memory, nbytes, script->principals, script->originPrincipals);
    js_free(memory);
    return script;
}

static JSScript *
GetScript(JSContext *cx, JS::HandleObject funobj)
{
    return JS_GetFunctionScript(cx, JS_GetObjectFunction(funobj));
}

JSObject *
FreezeThaw(JSContext *cx, JS::HandleObject funobj)
{
    // freeze
    uint32_t nbytes;
    void *memory = JS_EncodeInterpretedFunction(cx, funobj, &nbytes);
    if (!memory)
        return NULL;

    // thaw
    JSScript *script = GetScript(cx, funobj);
    JSObject *funobj2 = JS_DecodeInterpretedFunction(cx, memory, nbytes,
                                          script->principals, script->originPrincipals);
    js_free(memory);
    return funobj2;
}

static JSPrincipals testPrincipals[] = {
    { 1 },
    { 1 },
};

BEGIN_TEST(testXDR_principals)
{
    JSScript *script;
    JSCompartment *compartment = js::GetContextCompartment(cx);
    for (int i = TEST_FIRST; i != TEST_END; ++i) {
        script = createScriptViaXDR(NULL, NULL, i);
        CHECK(script);
        CHECK(!JS_GetScriptPrincipals(script));
        CHECK(!JS_GetScriptOriginPrincipals(script));

        script = createScriptViaXDR(NULL, NULL, i);
        CHECK(script);
        CHECK(!JS_GetScriptPrincipals(script));
        CHECK(!JS_GetScriptOriginPrincipals(script));

        // Appease the new JSAPI assertions. The stuff being tested here is
        // going away anyway.
        JSPrincipals *old = JS_GetCompartmentPrincipals(compartment);
        JS_SetCompartmentPrincipals(compartment, &testPrincipals[0]);
        script = createScriptViaXDR(&testPrincipals[0], NULL, i);
        CHECK(script);
        CHECK(JS_GetScriptPrincipals(script) == &testPrincipals[0]);
        CHECK(JS_GetScriptOriginPrincipals(script) == &testPrincipals[0]);

        script = createScriptViaXDR(&testPrincipals[0], &testPrincipals[0], i);
        CHECK(script);
        CHECK(JS_GetScriptPrincipals(script) == &testPrincipals[0]);
        CHECK(JS_GetScriptOriginPrincipals(script) == &testPrincipals[0]);

        script = createScriptViaXDR(&testPrincipals[0], &testPrincipals[1], i);
        CHECK(script);
        CHECK(JS_GetScriptPrincipals(script) == &testPrincipals[0]);
        CHECK(JS_GetScriptOriginPrincipals(script) == &testPrincipals[1]);

        script = createScriptViaXDR(NULL, &testPrincipals[1], i);
        CHECK(script);
        CHECK(!JS_GetScriptPrincipals(script));
        CHECK(JS_GetScriptOriginPrincipals(script) == &testPrincipals[1]);
        JS_SetCompartmentPrincipals(compartment, old);
    }

    return true;
}

enum TestCase {
    TEST_FIRST,
    TEST_SCRIPT = TEST_FIRST,
    TEST_FUNCTION,
    TEST_SERIALIZED_FUNCTION,
    TEST_END
};

JSScript *createScriptViaXDR(JSPrincipals *prin, JSPrincipals *orig, int testCase)
{
    const char src[] =
        "function f() { return 1; }\n"
        "f;\n";

    js::RootedObject global(cx, JS_GetGlobalObject(cx));
    JSScript *script = CompileScriptForPrincipalsVersionOrigin(cx, global, prin, orig,
                                                               src, strlen(src), "test", 1,
                                                               JSVERSION_DEFAULT);
    if (!script)
        return NULL;

    if (testCase == TEST_SCRIPT || testCase == TEST_SERIALIZED_FUNCTION) {
        script = FreezeThaw(cx, script);
        if (!script)
            return NULL;
        if (testCase == TEST_SCRIPT)
            return script;
    }

    js::RootedValue v(cx);
    JSBool ok = JS_ExecuteScript(cx, global, script, v.address());
    if (!ok || !v.isObject())
        return NULL;
    js::RootedObject funobj(cx, &v.toObject());
    if (testCase == TEST_FUNCTION) {
        funobj = FreezeThaw(cx, funobj);
        if (!funobj)
            return NULL;
    }
    return GetScript(cx, funobj);
}

END_TEST(testXDR_principals)

BEGIN_TEST(testXDR_atline)
{
    JS_ToggleOptions(cx, JSOPTION_ATLINE);
    CHECK(JS_GetOptions(cx) & JSOPTION_ATLINE);

    const char src[] =
"//@line 100 \"foo\"\n"
"function nested() { }\n"
"//@line 200 \"bar\"\n"
"nested;\n";

    JSScript *script = JS_CompileScript(cx, global, src, strlen(src), "internal", 1);
    CHECK(script);
    CHECK(script = FreezeThaw(cx, script));
    CHECK(!strcmp("bar", JS_GetScriptFilename(cx, script)));

    js::RootedValue v(cx);
    JSBool ok = JS_ExecuteScript(cx, global, script, v.address());
    CHECK(ok);
    CHECK(v.isObject());

    js::RootedObject funobj(cx, &v.toObject());
    script = JS_GetFunctionScript(cx, JS_GetObjectFunction(funobj));
    CHECK(!strcmp("foo", JS_GetScriptFilename(cx, script)));

    return true;
}

END_TEST(testXDR_atline)

BEGIN_TEST(testXDR_bug506491)
{
    const char *s =
        "function makeClosure(s, name, value) {\n"
        "    eval(s);\n"
        "    Math.sin(value);\n"
        "    return let (n = name, v = value) function () { return String(v); };\n"
        "}\n"
        "var f = makeClosure('0;', 'status', 'ok');\n";

    // compile
    JSScript *script = JS_CompileScript(cx, global, s, strlen(s), __FILE__, __LINE__);
    CHECK(script);

    script = FreezeThaw(cx, script);
    CHECK(script);

    // execute
    js::RootedValue v2(cx);
    CHECK(JS_ExecuteScript(cx, global, script, v2.address()));

    // try to break the Block object that is the parent of f
    JS_GC(rt);

    // confirm
    EVAL("f() === 'ok';\n", v2.address());
    js::RootedValue trueval(cx, JSVAL_TRUE);
    CHECK_SAME(v2, trueval);
    return true;
}
END_TEST(testXDR_bug506491)

BEGIN_TEST(testXDR_bug516827)
{
    // compile an empty script
    JSScript *script = JS_CompileScript(cx, global, "", 0, __FILE__, __LINE__);
    CHECK(script);

    script = FreezeThaw(cx, script);
    CHECK(script);

    // execute with null result meaning no result wanted
    CHECK(JS_ExecuteScript(cx, global, script, NULL));
    return true;
}
END_TEST(testXDR_bug516827)

BEGIN_TEST(testXDR_source)
{
    const char *samples[] = {
        // This can't possibly fail to compress well, can it?
        "function f(x) { return x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x + x }",
        "short",
        NULL
    };
    for (const char **s = samples; *s; s++) {
        JSScript *script = JS_CompileScript(cx, global, *s, strlen(*s), __FILE__, __LINE__);
        CHECK(script);
        script = FreezeThaw(cx, script);
        CHECK(script);
        JSString *out = JS_DecompileScript(cx, script, "testing", 0);
        CHECK(out);
        JSBool equal;
        CHECK(JS_StringEqualsAscii(cx, out, *s, &equal));
        CHECK(equal);
    }
    return true;
}
END_TEST(testXDR_source)

BEGIN_TEST(testXDR_sourceMap)
{
    const char *sourceMaps[] = {
        "http://example.com/source-map.json",
        "file:///var/source-map.json",
        NULL
    };
    js::RootedScript script(cx);
    for (const char **sm = sourceMaps; *sm; sm++) {
        script = JS_CompileScript(cx, global, "", 0, __FILE__, __LINE__);
        CHECK(script);

        size_t len = strlen(*sm);
        jschar *expected = js::InflateString(cx, *sm, &len);
        CHECK(expected);

        // The script source takes responsibility of free'ing |expected|.
        CHECK(script->scriptSource()->setSourceMap(cx, expected, script->filename));
        script = FreezeThaw(cx, script);
        CHECK(script);
        CHECK(script->scriptSource());
        CHECK(script->scriptSource()->hasSourceMap());

        const jschar *actual = script->scriptSource()->sourceMap();
        CHECK(actual);

        while (*expected) {
            CHECK(*actual);
            CHECK(*expected == *actual);
            expected++;
            actual++;
        }
        CHECK(!*actual);
    }
    return true;
}
END_TEST(testXDR_sourceMap)
