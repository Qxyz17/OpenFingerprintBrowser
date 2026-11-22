// 运行在 document_start，早于任何页面脚本。
// 所有改写的函数都登记进 fakeMap，toString 时返回 native code，避免直接暴露。
(function () {
  'use strict';

  var CFG = __PX_CFG__;

  var nativeToString = Function.prototype.toString;
  var fakeMap = new WeakMap();

  var toStringProxy = new Proxy(nativeToString, {
    apply: function (target, thisArg, args) {
      if (fakeMap.has(thisArg)) {
        return fakeMap.get(thisArg);
      }
      return Reflect.apply(target, thisArg, args);
    }
  });
  Function.prototype.toString = toStringProxy;
  fakeMap.set(toStringProxy, 'function toString() { [native code] }');

  function mask(fn, name) {
    fakeMap.set(fn, 'function ' + (name || '') + '() { [native code] }');
    return fn;
  }

  function maskGetter(fn, name) {
    fakeMap.set(fn, 'function get ' + (name || '') + '() { [native code] }');
    return fn;
  }

  function overrideGetter(proto, prop, make) {
    var desc = Object.getOwnPropertyDescriptor(proto, prop);
    if (!desc || !desc.configurable) {
      return;
    }
    Object.defineProperty(proto, prop, {
      get: maskGetter(make(desc.get), prop),
      set: desc.set,
      configurable: true,
      enumerable: desc.enumerable
    });
  }

  function overrideValue(proto, prop, make) {
    var desc = Object.getOwnPropertyDescriptor(proto, prop);
    if (!desc) {
      return;
    }
    if (!desc.configurable) {
      return;
    }
    Object.defineProperty(proto, prop, {
      value: mask(make(desc.value), prop),
      writable: desc.writable,
      configurable: true,
      enumerable: desc.enumerable
    });
  }

  function overrideGlobal(owner, prop, make) {
    var desc = Object.getOwnPropertyDescriptor(owner, prop);
    if (!desc || !desc.configurable) {
      return;
    }
    Object.defineProperty(owner, prop, {
      get: maskGetter(make(desc.get), prop),
      set: desc.set,
      configurable: true,
      enumerable: desc.enumerable
    });
  }

  // ---- 时区与区域 ----

  var tzName = CFG.timezone;
  var tzOffset = CFG.timezoneOffset;

  overrideValue(Date.prototype, 'getTimezoneOffset', function () {
    return function getTimezoneOffset() {
      return -tzOffset;
    };
  });

  var RealDTF = Intl.DateTimeFormat;
  var RealNF = Intl.NumberFormat;
  var RealCollator = Intl.Collator;
  var RealPlural = Intl.PluralRules;
  var RealRelative = Intl.RelativeTimeFormat;
  var RealList = Intl.ListFormat;

  function withDefaults(options, needsTimeZone) {
    var copy = {};
    if (options) {
      for (var k in options) {
        copy[k] = options[k];
      }
    }
    if (!copy.localeMatcher) {
      copy.localeMatcher = 'lookup';
    }
    if (needsTimeZone && !copy.timeZone) {
      copy.timeZone = tzName;
    }
    return copy;
  }

  function wrapIntlCtor(Real, needsTimeZone) {
    return new Proxy(Real, {
      construct: function (target, args) {
        var locales = args.length > 0 ? args[0] : CFG.locales;
        var options = args.length > 1 ? args[1] : undefined;
        return Reflect.construct(target, [locales, withDefaults(options, needsTimeZone)]);
      },
      apply: function (target, thisArg, args) {
        var locales = args.length > 0 ? args[0] : CFG.locales;
        var options = args.length > 1 ? args[1] : undefined;
        return Reflect.apply(target, thisArg, [locales, withDefaults(options, needsTimeZone)]);
      }
    });
  }

  Intl.DateTimeFormat = wrapIntlCtor(RealDTF, true);
  Intl.NumberFormat = wrapIntlCtor(RealNF, false);
  Intl.Collator = wrapIntlCtor(RealCollator, false);
  Intl.PluralRules = wrapIntlCtor(RealPlural, false);
  Intl.RelativeTimeFormat = wrapIntlCtor(RealRelative, false);
  Intl.ListFormat = wrapIntlCtor(RealList, false);

  // 显式传了别的时区时仍然报成配置值，避免 resolvedOptions 泄漏
  overrideValue(Intl.DateTimeFormat.prototype, 'resolvedOptions', function (orig) {
    return function resolvedOptions() {
      var opts = orig.call(this);
      opts.timeZone = tzName;
      opts.locale = CFG.locale;
      return opts;
    };
  });

  overrideValue(Intl.NumberFormat.prototype, 'resolvedOptions', function (orig) {
    return function resolvedOptions() {
      var opts = orig.call(this);
      opts.locale = CFG.locale;
      return opts;
    };
  });

  overrideValue(Date.prototype, 'toLocaleString', function (orig) {
    return function toLocaleString(locales, options) {
      if (locales === undefined) {
        locales = CFG.locales;
      }
      return orig.call(this, locales, withDefaults(options, true));
    };
  });

  overrideValue(Date.prototype, 'toLocaleDateString', function (orig) {
    return function toLocaleDateString(locales, options) {
      if (locales === undefined) {
        locales = CFG.locales;
      }
      return orig.call(this, locales, withDefaults(options, true));
    };
  });

  overrideValue(Date.prototype, 'toLocaleTimeString', function (orig) {
    return function toLocaleTimeString(locales, options) {
      if (locales === undefined) {
        locales = CFG.locales;
      }
      return orig.call(this, locales, withDefaults(options, true));
    };
  });

  // 系统时区名会从 toString 里漏出来
  overrideValue(Date.prototype, 'toString', function (orig) {
    return function toString() {
      var text = orig.call(this);
      return text.replace(/\b(?:UTC|GMT)[+-]\d{4}\b/, 'GMT' + formatOffset(tzOffset));
    };
  });

  overrideValue(Date.prototype, 'toTimeString', function (orig) {
    return function toTimeString() {
      var text = orig.call(this);
      return text.replace(/\b(?:UTC|GMT)[+-]\d{4}\b/, 'GMT' + formatOffset(tzOffset));
    };
  });

  function formatOffset(minutes) {
    var sign = minutes < 0 ? '-' : '+';
    var abs = Math.abs(minutes);
    var hh = String(Math.floor(abs / 60)).padStart(2, '0');
    var mm = String(abs % 60).padStart(2, '0');
    return sign + hh + mm;
  }

  // ---- 语言 ----

  overrideGlobal(Navigator.prototype, 'language', function () {
    return function () { return CFG.language; };
  });

  overrideGlobal(Navigator.prototype, 'languages', function () {
    return function () { return Object.freeze(CFG.languages.slice()); };
  });

  // ---- 硬件与平台 ----

  overrideGlobal(Navigator.prototype, 'hardwareConcurrency', function () {
    return function () { return CFG.hardwareConcurrency; };
  });

  overrideGlobal(Navigator.prototype, 'deviceMemory', function () {
    return function () { return CFG.deviceMemory; };
  });

  overrideGlobal(Navigator.prototype, 'platform', function () {
    return function () { return CFG.platform; };
  });

  overrideGlobal(Navigator.prototype, 'maxTouchPoints', function () {
    return function () { return CFG.maxTouchPoints; };
  });

  if (CFG.userAgent) {
    overrideGlobal(Navigator.prototype, 'userAgent', function () {
      return function () { return CFG.userAgent; };
    });
    overrideGlobal(Navigator.prototype, 'appVersion', function () {
      return function () { return CFG.userAgent.replace(/^Mozilla\//, ''); };
    });
  }

  // ---- 屏幕 ----

  [['width', 'screenWidth'], ['height', 'screenHeight'],
   ['availWidth', 'screenAvailWidth'], ['availHeight', 'screenAvailHeight'],
   ['colorDepth', 'colorDepth'], ['pixelDepth', 'colorDepth']].forEach(function (pair) {
    overrideGlobal(Screen.prototype, pair[0], function () {
      return function () { return CFG[pair[1]]; };
    });
  });

  overrideGlobal(window, 'devicePixelRatio', function () {
    return function () { return CFG.devicePixelRatio; };
  });

  overrideGlobal(window, 'outerWidth', function () {
    return function () { return CFG.screenWidth; };
  });

  overrideGlobal(window, 'outerHeight', function () {
    return function () { return CFG.screenHeight; };
  });

  // ---- Canvas ----

  function hashNoise(seed) {
    var x = seed | 0;
    return function () {
      x = (x * 1103515245 + 12345) & 0x7fffffff;
      return x / 0x7fffffff;
    };
  }

  var canvasRand = hashNoise(CFG.seed);

  overrideValue(CanvasRenderingContext2D.prototype, 'getImageData', function (orig) {
    return function getImageData() {
      var data = orig.apply(this, arguments);
      if (!CFG.canvasNoise) {
        return data;
      }
      var pixels = data.data;
      for (var i = 0; i < pixels.length; i += 4) {
        // 只抖动 alpha 之外的分量一小格，视觉无差别，哈希会变
        if (pixels[i + 3] !== 0) {
          pixels[i] = Math.min(255, Math.max(0, pixels[i] + (canvasRand() < 0.5 ? 0 : 1)));
        }
      }
      return data;
    };
  });

  overrideValue(HTMLCanvasElement.prototype, 'toDataURL', function (orig) {
    return function toDataURL() {
      if (CFG.canvasBlock) {
        var ctx = this.getContext('2d');
        if (ctx && this.width && this.height) {
          ctx.fillStyle = CFG.canvasFill;
          ctx.fillRect(0, 0, this.width, this.height);
        }
      }
      return orig.apply(this, arguments);
    };
  });

  overrideValue(HTMLCanvasElement.prototype, 'toBlob', function (orig) {
    return function toBlob() {
      if (CFG.canvasBlock) {
        var ctx = this.getContext('2d');
        if (ctx && this.width && this.height) {
          ctx.fillStyle = CFG.canvasFill;
          ctx.fillRect(0, 0, this.width, this.height);
        }
      }
      return orig.apply(this, arguments);
    };
  });

  // ---- WebGL ----

  var webglOverrides = {
    37445: CFG.webglVendor,
    37446: CFG.webglRenderer,
    7936: CFG.webglVendor,
    7937: CFG.webglRenderer
  };

  function patchWebGL(proto) {
    if (!proto) {
      return;
    }
    overrideValue(proto, 'getParameter', function (orig) {
      return function getParameter(param) {
        if (Object.prototype.hasOwnProperty.call(webglOverrides, param)) {
          return webglOverrides[param];
        }
        return orig.call(this, param);
      };
    });
  }

  if (typeof WebGLRenderingContext !== 'undefined') {
    patchWebGL(WebGLRenderingContext.prototype);
  }
  if (typeof WebGL2RenderingContext !== 'undefined') {
    patchWebGL(WebGL2RenderingContext.prototype);
  }

  // ---- 字体 ----

  var allowedFonts = Object.create(null);
  CFG.fonts.forEach(function (name) {
    allowedFonts[name.toLowerCase()] = true;
  });

  function fontInstalled(spec) {
    if (!spec) {
      return false;
    }
    // "12px Arial" / "bold 12px 'Noto Sans SC'"
    var m = /^\s*(?:[^"' ]+\s+)*(?:\d+(?:\.\d+)?)(?:px|pt|em|rem)?\s+(.+)$/.exec(spec);
    var family = m ? m[1] : spec;
    family = family.replace(/^["']|["']$/g, '').trim().toLowerCase();
    return allowedFonts[family] === true;
  }

  if (typeof document !== 'undefined' && document.fonts) {
    overrideValue(FontFaceSet.prototype, 'check', function (orig) {
      return function check(spec) {
        return fontInstalled(String(spec));
      };
    });
  }

  // 文本宽度探针是字体枚举的主要手段，把宽度对齐到白名单结果
  overrideValue(CanvasRenderingContext2D.prototype, 'measureText', function (orig) {
    return function measureText(text) {
      var metrics = orig.call(this, text);
      var font = this.font || '';
      if (!fontInstalled(font)) {
        // 未登记的字体按回退字体处理，宽度改写成一致的估算值
        try {
          Object.defineProperty(metrics, 'width', {
            value: String(text).length * CFG.fontFallbackWidth,
            configurable: true
          });
        } catch (e) {
        }
      }
      return metrics;
    };
  });

  // ---- AudioContext ----

  var audioRand = hashNoise(CFG.seed ^ 0x5f3759df);

  if (typeof AnalyserNode !== 'undefined') {
    overrideValue(AnalyserNode.prototype, 'getFloatFrequencyData', function (orig) {
      return function getFloatFrequencyData(array) {
        orig.call(this, array);
        if (!CFG.audioNoise) {
          return;
        }
        for (var i = 0; i < array.length; i++) {
          array[i] += (audioRand() - 0.5) * 1e-7;
        }
      };
    });

    overrideValue(AnalyserNode.prototype, 'getByteFrequencyData', function (orig) {
      return function getByteFrequencyData(array) {
        orig.call(this, array);
        if (!CFG.audioNoise) {
          return;
        }
        for (var i = 0; i < array.length; i++) {
          if (array[i] > 0 && array[i] < 255) {
            array[i] += audioRand() < 0.5 ? -1 : 1;
          }
        }
      };
    });
  }

  // ---- WebRTC ----

  if (CFG.webrtcMode !== 'off') {
    var RealRTC = window.RTCPeerConnection;
    if (typeof RealRTC === 'function') {
      var realAddIce = RealRTC.prototype.addIceCandidate;
      var realSetLocal = RealRTC.prototype.setLocalDescription;
      var realCreateOffer = RealRTC.prototype.createOffer;

      var WrappedRTC = function RTCPeerConnection(config) {
        config = config ? JSON.parse(JSON.stringify(config)) : {};
        if (CFG.webrtcMode === 'block') {
          config.iceServers = [];
          if (!config.iceTransportPolicy) {
            config.iceTransportPolicy = 'relay';
          }
        }
        return new RealRTC(config);
      };

      WrappedRTC.prototype = RealRTC.prototype;
      Object.setPrototypeOf(WrappedRTC, RealRTC);
      Object.defineProperty(WrappedRTC, 'name', { value: 'RTCPeerConnection' });

      mask(WrappedRTC, 'RTCPeerConnection');
      window.RTCPeerConnection = WrappedRTC;

      // 收集到的候选里带内网地址，过滤掉 host 与 srflx
      overrideValue(RealRTC.prototype, 'setLocalDescription', function (orig) {
        return function setLocalDescription(desc) {
          if (desc && desc.sdp && CFG.webrtcMode === 'block') {
            desc = Object.assign({}, desc);
            desc.sdp = desc.sdp.split('\r\n').filter(function (line) {
              if (line.indexOf('a=candidate:') !== 0) {
                return true;
              }
              return line.indexOf(' typ host') === -1 && line.indexOf(' typ srflx') === -1;
            }).join('\r\n');
          }
          return orig.call(this, desc);
        };
      });
    }
  }

  // ---- 插件列表 ----

  overrideGlobal(Navigator.prototype, 'plugins', function () {
    return function () {
      var list = CFG.plugins.map(function (name) {
        return { name: name, filename: name.toLowerCase() + '.dll', description: name + ' Plugin' };
      });
      list.item = function (i) { return list[i] || null; };
      list.namedItem = function (n) {
        for (var i = 0; i < list.length; i++) {
          if (list[i].name === n) {
            return list[i];
          }
        }
        return null;
      };
      list.refresh = function () {};
      return list;
    };
  });

  overrideGlobal(Navigator.prototype, 'mimeTypes', function () {
    return function () {
      var list = [];
      list.item = function () { return null; };
      list.namedItem = function () { return null; };
      return list;
    };
  });
})();
