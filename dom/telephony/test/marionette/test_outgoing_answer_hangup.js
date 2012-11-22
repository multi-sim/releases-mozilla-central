/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 10000;

SpecialPowers.addPermission("telephony", true, document);

let mgr = window.navigator.mozTelephonyManager;
let activeTelephony = mgr.defaultPhone;
let idleTelephony = mgr.phones[1];
let number = "5555552368";
let outgoing;
let is2ndTest = false;
let pendingEmulatorCmdCount = 0;

function verifyInitialState() {
  log("Run 1st test: using modem 0");
  ok(mgr);
  ok(mgr.phones);
  is(mgr.phones.length, 2);
  is(mgr.phones[0], activeTelephony);
  is(mgr.phones[1], idleTelephony);

  is(activeTelephony.active, null);
  is(idleTelephony.active, null);
  ok(mgr.calls);
  ok(activeTelephony.calls);
  ok(idleTelephony.calls);

  is(mgr.calls.length, 0);
  is(activeTelephony.calls.length, 0);
  is(idleTelephony.calls.length, 0);

  pendingEmulatorCmdCount++;
  runEmulatorCmd("gsm list", function(result) {
    pendingEmulatorCmdCount--;

    log("Initial call list: " + result);
    is(result[0], "OK");
    dial();
  });
}

function dial() {
  log("Make an outgoing call.");

  outgoing = activeTelephony.dial(number);
  ok(outgoing);
  is(outgoing.number, number);
  is(outgoing.state, "dialing");

  is(outgoing, activeTelephony.active);
  is(mgr.calls.length, 1);
  is(activeTelephony.calls.length, 1);
  is(idleTelephony.calls.length, 0);
  is(mgr.calls[0], outgoing);
  is(activeTelephony.calls[0], outgoing);

  outgoing.onalerting = function onalerting(event) {
    log("Received 'onalerting' call event.");
    is(outgoing, event.call);
    is(outgoing.state, "alerting");

    pendingEmulatorCmdCount++;
    runEmulatorCmd("gsm list", function(result) {
      pendingEmulatorCmdCount--;

      log("Call list is now: " + result);
      is(result[0], "outbound to  " + number + " : ringing");
      is(result[1], "OK");
      answer();
    });
  };
}

function answer() {
  log("Answering the outgoing call.");

  // We get no "connecting" event when the remote party answers the call.

  outgoing.onconnected = function onconnected(event) {
    log("Received 'connected' call event.");
    is(outgoing, event.call);
    is(outgoing.state, "connected");

    is(outgoing, activeTelephony.active);
    pendingEmulatorCmdCount++;
    runEmulatorCmd("gsm list", function(result) {
      pendingEmulatorCmdCount--;

      log("Call list is now: " + result);
      is(result[0], "outbound to  " + number + " : active");
      is(result[1], "OK");
      hangUp();
    });
  };
  runEmulatorCmd("gsm accept " + number);
};

function hangUp() {
  log("Hanging up the outgoing call.");

  // We get no "disconnecting" event when the remote party terminates the call.

  outgoing.ondisconnected = function ondisconnected(event) {
    log("Received 'disconnected' call event.");
    is(outgoing, event.call);
    is(outgoing.state, "disconnected");
    is(mgr.calls.length, 0);
    is(activeTelephony.active, null);
    is(activeTelephony.calls.length, 0);
    is(idleTelephony.calls.length, 0);

    pendingEmulatorCmdCount++;
    runEmulatorCmd("gsm list", function(result) {
      pendingEmulatorCmdCount--;

      log("Call list is now: " + result);
      is(result[0], "OK");
      if (is2ndTest) {
        cleanUp();
      } else {
        runNextTest();
      }
    });
  };
  runEmulatorCmd("gsm cancel " + number);
}

function runNextTest() {
  log("Run next test: using modem 1");
  is2ndTest = true;

  pendingEmulatorCmdCount++;
  runEmulatorCmd("mux select 1", function(result) {
    pendingEmulatorCmdCount--;

    is(result[0], "OK");

    activeTelephony = mgr.phones[1];
    idleTelephony = mgr.phones[0];
  });

  ok(activeTelephony);
  ok(idleTelephony);
  is(activeTelephony.active, null);
  is(idleTelephony.active, null);
  ok(activeTelephony.calls);
  ok(idleTelephony.calls);
  is(mgr.calls.length, 0);
  is(activeTelephony.calls.length, 0);
  is(idleTelephony.calls.length, 0);

  pendingEmulatorCmdCount++;
  runEmulatorCmd("gsm list", function(result) {
    log("Initial call list: " + result);
    pendingEmulatorCmdCount--;
    is(result[0], "OK");
    dial();
  });
}

function cleanUp() {
  if (pendingEmulatorCmdCount > 0) {
    window.setTimeout(cleanUp, 100);
    return;
  }
  SpecialPowers.removePermission("telephony", document);
  finish();
}

verifyInitialState();
