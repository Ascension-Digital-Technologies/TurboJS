/* TurboJS Test262 host adapter. */
(function (global) {
    function failUnsupported(name) {
        throw new Test262Error("TurboJS host hook is not available: " + name);
    }
    global.$262 = {
        global: global,
        evalScript: function (source) { return (0, eval)(String(source)); },
        gc: typeof gc === "function" ? gc : function () {},
        createRealm: function () { return failUnsupported("createRealm"); },
        detachArrayBuffer: function () { return failUnsupported("detachArrayBuffer"); },
        agent: {
            start: function () { return failUnsupported("agent.start"); },
            broadcast: function () { return failUnsupported("agent.broadcast"); },
            getReport: function () { return null; },
            sleep: function () { return failUnsupported("agent.sleep"); },
            monotonicNow: function () { return Date.now(); }
        }
    };
    global.$DONE = function (error) {
        if (error !== undefined) throw error;
    };
})(globalThis);
