// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var pass = chrome.test.callbackPass;
var dataURL = 'data:text/plain,redirected1';

function getURLNonWebAccessible() {
  return getURL('manifest.json');
}

function getURLWebAccessible() {
  return getURL('simpleLoad/a.html');
}

function assertRedirectSucceeds(url, redirectURL, callback) {
  // Load a page to be sure webRequest listeners are set up.
  navigateAndWait(getURL("simpleLoad/a.html"), function() {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.onload = pass(function() {
      callback && callback();
      chrome.test.assertEq(xhr.responseURL, redirectURL);
    });
    xhr.onerror = function() {
      callback && callback();
      chrome.test.fail();
    }
    xhr.send();
  });
}

function assertRedirectFails(url) {
  // Load a page to be sure webRequest listeners are set up.
  navigateAndWait(getURL("simpleLoad/a.html"), function() {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.onload = function() {
      chrome.test.fail();
    };
    xhr.onerror = pass(function() {});
    xhr.send();
  });
}

runTests([
  function subresourceRedirectToDataUrlOnHeadersReceived() {
    var url = getServerURL('echo');
    var listener = function(details) {
      return {redirectUrl: dataURL};
    };
    chrome.webRequest.onHeadersReceived.addListener(listener,
        {urls: [url]}, ['blocking']);

    assertRedirectSucceeds(url, dataURL, function() {
      chrome.webRequest.onHeadersReceived.removeListener(listener);
    });
  },

  function subresourceRedirectToNonWebAccessibleUrlOnHeadersReceived() {
    var url = getServerURL('echo');
    var listener = function(details) {
      return {redirectUrl: getURLNonWebAccessible()};
    };
    chrome.webRequest.onHeadersReceived.addListener(listener,
        {urls: [url]}, ['blocking']);

    assertRedirectSucceeds(url, getURLNonWebAccessible(), function() {
      chrome.webRequest.onHeadersReceived.removeListener(listener);
    });
  },

  function subresourceRedirectToServerRedirectOnHeadersReceived() {
    var url = getServerURL('echo');
    var redirectURL = getServerURL('server-redirect?' + getURLWebAccessible());
    var listener = function(details) {
      return {redirectUrl: redirectURL};
    };
    chrome.webRequest.onHeadersReceived.addListener(listener,
        {urls: [url]}, ['blocking']);

    assertRedirectSucceeds(url, getURLWebAccessible(), function() {
      chrome.webRequest.onHeadersReceived.removeListener(listener);
    });
  },

  function subresourceRedirectToDataUrlOnBeforeRequest() {
    var url = getServerURL('echo');
    var listener = function(details) {
      return {redirectUrl: dataURL};
    };
    chrome.webRequest.onBeforeRequest.addListener(listener,
        {urls: [url]}, ['blocking']);

    assertRedirectSucceeds(url, dataURL, function() {
      chrome.webRequest.onBeforeRequest.removeListener(listener);
    });
  },

  function subresourceRedirectToNonWebAccessibleUrlOnBeforeRequest() {
    var url = getServerURL('echo');
    var listener = function(details) {
      return {redirectUrl: getURLNonWebAccessible()};
    };
    chrome.webRequest.onBeforeRequest.addListener(listener,
        {urls: [url]}, ['blocking']);

    assertRedirectSucceeds(url, getURLNonWebAccessible(), function() {
      chrome.webRequest.onBeforeRequest.removeListener(listener);
    });
  },

  function subresourceRedirectToServerRedirectOnBeforeRequest() {
    var url = getServerURL('echo');
    var redirectURL = getServerURL('server-redirect?' + getURLWebAccessible());
    var listener = function(details) {
      return {redirectUrl: redirectURL};
    };
    chrome.webRequest.onBeforeRequest.addListener(listener,
        {urls: [url]}, ['blocking']);

    assertRedirectSucceeds(url, getURLWebAccessible(), function() {
      chrome.webRequest.onBeforeRequest.removeListener(listener);
    });
  },

  function subresourceRedirectToDataUrlWithServerRedirect() {
    assertRedirectFails(getServerURL('server-redirect?' + dataURL));
  },

  function subresourceRedirectToNonWebAccessibleWithServerRedirect() {
    assertRedirectFails(
        getServerURL('server-redirect?' + getURLNonWebAccessible()));
  },

  function subresourceRedirectToWebAccessibleWithServerRedirect() {
    assertRedirectSucceeds(
        getServerURL('server-redirect?' + getURLWebAccessible()),
        getURLWebAccessible());
  },
]);
