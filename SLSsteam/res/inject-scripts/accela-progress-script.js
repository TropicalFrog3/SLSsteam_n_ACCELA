// accela-progress-script.js
// Injected into Steam library / Big Picture pages (non-SharedJSContext).
// Polls /active-downloads, /progress, and /check on port 9001 to show live
// download progress for any game being installed via accela-helper.
//
// Dual-display architecture with IN-PLACE DOM updates:
// 1. Inline Card: Rendered inside the game details action button row (next to Manage / SLS buttons).
// 2. Global Floating Indicator: Sleek, non-intrusive status pill at bottom-right when navigating elsewhere in Steam.
//
// CRITICAL: All DOM nodes (especially buttons) are created ONCE and updated in-place.
// We NEVER wipe innerHTML on poll cycles so Steam Gamepad Focus nodes and controller
// selection are preserved 100% seamlessly without jumping or focus loss.
//
// Guard flag: window.__slsAccelaProgressInjected

(function () {
    'use strict';
    if (window.__slsAccelaProgressInjected) return;
    window.__slsAccelaProgressInjected = true;

    // ── Constants & Configuration ─────────────────────────────────────────────

    var BASE_URL = 'http://127.0.0.1:9001';
    var POLL_ACTIVE_MS = 1500;
    var POLL_IDLE_MS   = 3000;

    var PHASE_LABELS = {
        'starting':       'Starting…',
        'preparing':      'Preparing files…',
        'downloading':    'Downloading…',
        'paused':         'Paused',
        'postprocessing': 'Finalising…',
        'done':           'Installed ✓',
        'failed':         'Download failed',
        'idle':           'Idle'
    };

    // ── Steam Gamepad Focus Node Registration ──────────────────────────────────

    function registerSteamFocusNode(element, properties) {
        if (!element) return null;
        if (!properties) properties = { focusable: true };
        if (!element.hasAttribute('tabindex')) element.setAttribute('tabindex', '-1');
        element.classList.add('Focusable');
        var parent = element.parentElement;
        var parentNode = null;
        while (parent && parent !== document.body) {
            var fiberKey = Object.keys(parent).find(function(key) { return key.indexOf('__reactFiber') === 0; });
            if (fiberKey) {
                var n = parent[fiberKey];
                while (n) {
                    if (n.memoizedProps && n.memoizedProps.node) {
                        parentNode = n.memoizedProps.node;
                        break;
                    }
                    n = n.return;
                }
            }
            if (parentNode) break;
            parent = parent.parentElement;
        }
        if (!parentNode) return null;
        try {
            var NavNodeClass = parentNode.constructor;
            var newNode = new NavNodeClass(parentNode.m_Tree, parentNode, null);
            if (newNode.SetProperties) newNode.SetProperties(properties);
            else newNode.m_Properties = Object.assign(newNode.m_Properties || {}, properties);
            if (newNode.OnMount) newNode.OnMount(element);
            return newNode;
        } catch (e) {
            return null;
        }
    }

    // ── Stylesheet ────────────────────────────────────────────────────────────

    function ensureStyles() {
        if (document.getElementById('sls-dl-styles')) return;
        var s = document.createElement('style');
        s.id = 'sls-dl-styles';
        s.textContent = [
            // Inline Card (Inside Game Details Button Row)
            '.sls-dl-card {',
            '  display: inline-flex; align-items: center; gap: 8px;',
            '  background: linear-gradient(135deg, #1b2838 0%, #151a21 100%);',
            '  border: 1px solid rgba(103, 193, 245, 0.3); border-radius: 4px;',
            '  padding: 6px 12px; margin-right: 8px; min-width: 250px;',
            '  height: 48px; box-sizing: border-box;',
            '  box-shadow: 0 4px 16px rgba(0,0,0,0.5), inset 0 0 12px rgba(103,193,245,0.06);',
            '  font-family: "Motiva Sans", Arial, sans-serif;',
            '  vertical-align: middle; z-index: 10;',
            '  transition: all 0.2s ease;',
            '}',
            // Icons & Animations
            '.sls-dl-icon-wrap, .sls-floating-icon-wrap { flex-shrink: 0; display: flex; align-items: center; justify-content: center; }',
            '.sls-dl-icon { flex-shrink: 0; animation: sls-spin 1.4s linear infinite; }',
            '.sls-dl-icon.done { animation: none; }',
            '.sls-dl-icon.paused { animation: none; opacity: 0.6; }',
            '@keyframes sls-spin { to { transform: rotate(360deg); } }',
            '@keyframes sls-slide-up { from { opacity: 0; transform: translateY(16px); } to { opacity: 1; transform: translateY(0); } }',
            // Card Content
            '.sls-dl-body { display: flex; flex-direction: column; gap: 3px; flex: 1; min-width: 0; }',
            '.sls-dl-header { display: flex; justify-content: space-between; align-items: baseline; gap: 6px; }',
            '.sls-dl-game { font-size: 12px; font-weight: 700; color: #e1e7ed; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 120px; }',
            '.sls-dl-pct { font-size: 12px; font-weight: 700; color: #67c1f5; flex-shrink: 0; }',
            '.sls-dl-track { height: 4px; background: rgba(255,255,255,0.12); border-radius: 2px; overflow: hidden; }',
            '.sls-dl-fill { height: 100%; border-radius: 2px; background: linear-gradient(90deg, #1a9fff, #00d4ff); transition: width 0.4s ease; will-change: width; }',
            '.sls-dl-fill.failed { background: #ef4444; }',
            '.sls-dl-fill.done { background: #10b981; }',
            '.sls-dl-fill.paused { background: linear-gradient(90deg, #4b5563, #6b7280); }',
            '.sls-dl-meta { display: flex; justify-content: space-between; align-items: center; gap: 6px; }',
            '.sls-dl-phase { font-size: 10px; color: #8ba3b8; text-transform: uppercase; letter-spacing: .04em; flex-shrink: 0; }',
            '.sls-dl-speed { font-size: 10px; color: #67c1f5; font-weight: 600; flex-shrink: 0; }',
            '.sls-dl-eta { font-size: 10px; color: #94a3b8; flex-shrink: 0; }',
            // Button Controls (Preserved in DOM across all polls!)
            '.sls-dl-controls { display: flex; gap: 4px; flex-shrink: 0; align-items: center; margin-left: 2px; }',
            '.sls-dl-btn { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15);',
            '  color: #cbd5e1; cursor: pointer; font-size: 11px; line-height: 1; padding: 4px 8px;',
            '  border-radius: 3px; transition: all 0.15s ease; white-space: nowrap; outline: none; }',
            '.sls-dl-btn:hover { background: rgba(255,255,255,0.18); color: #fff; }',
            '.sls-dl-btn.pause:hover { background: rgba(251,191,36,0.2); color: #fbbf24; border-color: rgba(251,191,36,0.4); }',
            '.sls-dl-btn.resume:hover { background: rgba(52,211,153,0.2); color: #34d399; border-color: rgba(52,211,153,0.4); }',
            '.sls-dl-btn.cancel:hover { background: rgba(239,68,68,0.2); color: #f87171; border-color: rgba(239,68,68,0.4); }',
            '.sls-dl-btn:disabled { opacity: 0.35; cursor: default; pointer-events: none; }',
            // Floating Mini-Progress Bar (Global Navigation Fallback)
            '.sls-floating-dl-bar {',
            '  position: fixed; bottom: 28px; right: 28px; z-index: 999990;',
            '  display: flex; align-items: center; gap: 12px;',
            '  background: rgba(20, 25, 34, 0.96);',
            '  border: 1px solid rgba(103, 193, 245, 0.4);',
            '  box-shadow: 0 8px 32px rgba(0,0,0,0.7), 0 0 16px rgba(103,193,245,0.15);',
            '  backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px);',
            '  border-radius: 8px; padding: 10px 16px; min-width: 320px; max-width: 440px;',
            '  font-family: "Motiva Sans", Arial, sans-serif; color: #e2e8f0;',
            '  animation: sls-slide-up 0.3s cubic-bezier(0.16, 1, 0.3, 1);',
            '  transition: opacity 0.25s ease, transform 0.25s ease;',
            '}',
            '.sls-floating-dl-bar.hiding { opacity: 0; transform: translateY(12px); pointer-events: none; }',
            '.sls-floating-body { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 4px; }',
            '.sls-floating-top { display: flex; justify-content: space-between; align-items: center; gap: 8px; }',
            '.sls-floating-title { font-size: 13px; font-weight: 700; color: #f8fafc; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }',
            '.sls-floating-pct { font-size: 13px; font-weight: 700; color: #67c1f5; }',
            '.sls-floating-sub { display: flex; justify-content: space-between; font-size: 11px; color: #94a3b8; }'
        ].join('');
        document.head.appendChild(s);
    }
    ensureStyles();

    // ── Formatters & SVG Helpers ──────────────────────────────────────────────

    function escHtml(s) {
        return String(s || '')
            .replace(/&/g, '&amp;').replace(/</g, '&lt;')
            .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    function formatSpeed(bps) {
        if (!bps || bps <= 0) return '';
        if (bps >= 1024 * 1024 * 1024) return (bps / (1024 * 1024 * 1024)).toFixed(1) + ' GB/s';
        if (bps >= 1024 * 1024)        return (bps / (1024 * 1024)).toFixed(1) + ' MB/s';
        if (bps >= 1024)               return (bps / 1024).toFixed(0) + ' KB/s';
        return bps + ' B/s';
    }

    function formatEta(sec) {
        if (sec == null || sec < 0) return '';
        if (sec < 60)  return sec + 's';
        if (sec < 3600) {
            var m = Math.floor(sec / 60), s = sec % 60;
            return m + 'm ' + (s > 0 ? s + 's' : '');
        }
        var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
        return h + 'h ' + (m > 0 ? m + 'm' : '');
    }

    function spinnerSVG(phase, size) {
        if (!size) size = 18;
        var isDone   = phase === 'done';
        var isFailed = phase === 'failed';
        var isPaused = phase === 'paused';

        if (isDone)
            return '<svg class="sls-dl-icon done" width="' + size + '" height="' + size + '" viewBox="0 0 24 24"'
                 + ' fill="none" stroke="#10b981" stroke-width="2.5"'
                 + ' stroke-linecap="round" stroke-linejoin="round">'
                 + '<polyline points="20 6 9 17 4 12"/></svg>';
        if (isFailed)
            return '<svg class="sls-dl-icon done" width="' + size + '" height="' + size + '" viewBox="0 0 24 24"'
                 + ' fill="none" stroke="#ef4444" stroke-width="2.5"'
                 + ' stroke-linecap="round" stroke-linejoin="round">'
                 + '<line x1="18" y1="6" x2="6" y2="18"/>'
                 + '<line x1="6" y1="6" x2="18" y2="18"/></svg>';
        if (isPaused)
            return '<svg class="sls-dl-icon paused" width="' + size + '" height="' + size + '" viewBox="0 0 24 24"'
                 + ' fill="none" stroke="#94a3b8" stroke-width="2.5"'
                 + ' stroke-linecap="round" stroke-linejoin="round">'
                 + '<rect x="6" y="4" width="4" height="16"/>'
                 + '<rect x="14" y="4" width="4" height="16"/></svg>';
        return '<svg class="sls-dl-icon" width="' + size + '" height="' + size + '" viewBox="0 0 24 24"'
             + ' fill="none" stroke="#67c1f5" stroke-width="2.5"'
             + ' stroke-linecap="round" stroke-linejoin="round">'
             + '<path d="M12 2a10 10 0 0 1 10 10" opacity=".3"/>'
             + '<path d="M12 2a10 10 0 0 0-10 10 10 10 0 0 0 10 10"/>'
             + '</svg>';
    }

    // ── AppID Extraction from Steam DOM ───────────────────────────────────────

    function extractAppId(elem) {
        var appid = null;
        var curr  = elem;
        while (curr && curr !== document.body) {
            var cls = (typeof curr.className === 'string') ? curr.className : '';
            var m = cls.match(/\bapp_([0-9]+)\b/);
            if (m) { appid = m[1]; break; }
            var da = curr.getAttribute('data-appid') || curr.getAttribute('data-sls-appid');
            if (da) { appid = da; break; }
            curr = curr.parentElement;
        }

        if (!appid) {
            try {
                curr = elem;
                while (curr && curr !== document.body && !appid) {
                    for (var k in curr) {
                        if (k.startsWith('__reactInternalInstance$') || k.startsWith('__reactFiber$')) {
                            var fiber = curr[k];
                            while (fiber) {
                                var p = fiber.memoizedProps;
                                if (p) {
                                    if (p.appid)   { appid = String(p.appid);   break; }
                                    if (p.appID)   { appid = String(p.appID);   break; }
                                    if (p.unAppID) { appid = String(p.unAppID); break; }
                                }
                                fiber = fiber.return;
                            }
                        }
                        if (appid) break;
                    }
                    curr = curr.parentElement;
                }
            } catch(e) {}
        }

        if (!appid) {
            var match = window.location.href.match(/\/app\/([0-9]+)/);
            if (match) appid = match[1];
            else {
                match = window.location.hash.match(/\/app\/([0-9]+)/);
                if (match) appid = match[1];
            }
        }
        return appid;
    }

    function findActionRow() {
        var manageBtn = document.querySelector('div[aria-label="Manage"], div[aria-label="Configure Controller"]');
        if (manageBtn && manageBtn.parentNode) {
            return manageBtn.parentNode.parentNode;
        }
        return document.querySelector('div._1thLDT_28YIf6OkgIb6n-4');
    }

    // ── Stable Inline Card Management (In-Place Updates) ──────────────────────

    function ensureInlineCard(actionRow, appid) {
        var card = actionRow.querySelector('.sls-dl-card[data-sls-app-id="' + appid + '"]');
        if (card && card.parentElement === actionRow) {
            if (actionRow.firstChild !== card) {
                actionRow.insertBefore(card, actionRow.firstChild);
            }
            return card;
        }

        ensureStyles();
        card = document.createElement('div');
        card.className = 'sls-dl-card';
        card.dataset.slsAppId = appid;

        card.innerHTML = [
            '<div class="sls-dl-icon-wrap"></div>',
            '<div class="sls-dl-body">',
            '  <div class="sls-dl-header">',
            '    <span class="sls-dl-game"></span>',
            '    <span class="sls-dl-pct"></span>',
            '  </div>',
            '  <div class="sls-dl-track"><div class="sls-dl-fill"></div></div>',
            '  <div class="sls-dl-meta">',
            '    <span class="sls-dl-phase"></span>',
            '    <span class="sls-dl-speed"></span>',
            '    <span class="sls-dl-eta"></span>',
            '  </div>',
            '</div>',
            '<div class="sls-dl-controls">',
            '  <button class="sls-dl-btn sls-btn-toggle pause Focusable" tabindex="-1">&#x23F8; Pause</button>',
            '  <button class="sls-dl-btn sls-btn-cancel cancel Focusable" tabindex="-1">&#x2715; Cancel</button>',
            '</div>'
        ].join('');

        actionRow.insertBefore(card, actionRow.firstChild);

        // Wire button events and Steam Gamepad focus ONCE at mount time
        var toggleBtn = card.querySelector('.sls-btn-toggle');
        var cancelBtn = card.querySelector('.sls-btn-cancel');

        toggleBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            toggleBtn.disabled = true;
            var isCurrentlyPaused = toggleBtn.classList.contains('resume');
            var endpoint = isCurrentlyPaused ? '/resume?id=' : '/pause?id=';
            fetch(BASE_URL + endpoint + appid).catch(function () {});
            setTimeout(syncDownloads, 200);
        };

        cancelBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            cancelBtn.disabled = true;
            fetch(BASE_URL + '/cancel?id=' + appid).catch(function () {});
            setTimeout(syncDownloads, 300);
        };

        [toggleBtn, cancelBtn].forEach(function (b) {
            registerSteamFocusNode(b);
            b.addEventListener('vgp_onfocus', function () { b.style.outline = '2px solid white'; b.style.outlineOffset = '2px'; });
            b.addEventListener('vgp_onblur',  function () { b.style.outline = 'none'; });
            b.addEventListener('vgp_onok',    function (e) { e.preventDefault(); e.stopPropagation(); b.click(); });
        });

        return card;
    }

    function updateInlineCard(card, dl) {
        var pct      = Math.max(0, Math.min(100, dl.percent || 0));
        var phase    = dl.phase || 'downloading';
        var isPaused = phase === 'paused' || dl.paused;
        var isDone   = phase === 'done';
        var isFailed = phase === 'failed';
        var name     = dl.gameName || ('App ' + dl.appId);

        // 1. Icon (only update innerHTML if phase changed)
        var iconWrap = card.querySelector('.sls-dl-icon-wrap');
        if (iconWrap && card.dataset.lastPhase !== phase) {
            iconWrap.innerHTML = spinnerSVG(phase, 18);
            card.dataset.lastPhase = phase;
        }

        // 2. Title & Percentage in-place
        var titleEl = card.querySelector('.sls-dl-game');
        if (titleEl && titleEl.textContent !== name) {
            titleEl.textContent = name;
            titleEl.title = name;
        }

        var pctEl = card.querySelector('.sls-dl-pct');
        var pctText = pct + '%';
        if (pctEl && pctEl.textContent !== pctText) {
            pctEl.textContent = pctText;
        }

        // 3. Track fill width & class in-place
        var fillEl = card.querySelector('.sls-dl-fill');
        if (fillEl) {
            var fillClass = isFailed ? 'sls-dl-fill failed'
                          : isDone   ? 'sls-dl-fill done'
                          : isPaused ? 'sls-dl-fill paused'
                          :            'sls-dl-fill';
            if (fillEl.className !== fillClass) fillEl.className = fillClass;
            fillEl.style.width = pct + '%';
        }

        // 4. Meta text (phase, speed, ETA) in-place
        var phaseEl = card.querySelector('.sls-dl-phase');
        if (phaseEl) {
            var label = (PHASE_LABELS[phase] || phase) + (dl.depotsTotal > 0 ? ' · ' + (dl.depotsDone || 0) + '/' + dl.depotsTotal : '');
            if (phaseEl.textContent !== label) phaseEl.textContent = label;
        }

        var speedEl = card.querySelector('.sls-dl-speed');
        if (speedEl) {
            var speedStr = (phase === 'downloading' && !isPaused) ? formatSpeed(dl.speedBps) : '';
            if (speedEl.textContent !== speedStr) speedEl.textContent = speedStr;
            speedEl.style.display = speedStr ? 'inline' : 'none';
        }

        var etaEl = card.querySelector('.sls-dl-eta');
        if (etaEl) {
            var etaStr = (phase === 'downloading' && !isPaused && dl.etaSec >= 0) ? '~' + formatEta(dl.etaSec) : '';
            if (etaEl.textContent !== etaStr) etaEl.textContent = etaStr;
            etaEl.style.display = etaStr ? 'inline' : 'none';
        }

        // 5. Controls: update button text/classes in-place without destroying DOM elements!
        // This keeps Steam gamepad focus node active on whichever button the user is focusing!
        var toggleBtn = card.querySelector('.sls-btn-toggle');
        if (toggleBtn) {
            if (isDone || isFailed) {
                toggleBtn.style.display = 'none';
            } else {
                toggleBtn.style.display = 'inline-block';
                toggleBtn.disabled = false;
                if (isPaused) {
                    if (!toggleBtn.classList.contains('resume')) {
                        toggleBtn.innerHTML = '&#x25B6; Resume';
                        toggleBtn.className = 'sls-dl-btn sls-btn-toggle resume Focusable';
                        toggleBtn.title = 'Resume download';
                    }
                } else {
                    if (!toggleBtn.classList.contains('pause')) {
                        toggleBtn.innerHTML = '&#x23F8; Pause';
                        toggleBtn.className = 'sls-dl-btn sls-btn-toggle pause Focusable';
                        toggleBtn.title = 'Pause download';
                    }
                }
            }
        }

        var cancelBtn = card.querySelector('.sls-btn-cancel');
        if (cancelBtn) {
            if (isDone || isFailed) {
                cancelBtn.style.display = 'none';
            } else {
                cancelBtn.style.display = 'inline-block';
                cancelBtn.disabled = false;
            }
        }
    }

    // ── Stable Floating Global Progress Bar (In-Place Updates) ────────────────

    function ensureFloatingBar(appid) {
        var bar = document.getElementById('sls-floating-dl-bar');
        if (bar) return bar;

        ensureStyles();
        bar = document.createElement('div');
        bar.id = 'sls-floating-dl-bar';
        bar.className = 'sls-floating-dl-bar';

        bar.innerHTML = [
            '<div class="sls-floating-icon-wrap"></div>',
            '<div class="sls-floating-body">',
            '  <div class="sls-floating-top">',
            '    <span class="sls-floating-title"></span>',
            '    <span class="sls-floating-pct"></span>',
            '  </div>',
            '  <div class="sls-dl-track"><div class="sls-dl-fill"></div></div>',
            '  <div class="sls-floating-sub">',
            '    <span class="sls-floating-phase"></span>',
            '    <span class="sls-floating-meta"></span>',
            '  </div>',
            '</div>',
            '<div class="sls-dl-controls">',
            '  <button class="sls-dl-btn sls-btn-toggle pause Focusable" tabindex="-1">&#x23F8; Pause</button>',
            '  <button class="sls-dl-btn sls-btn-cancel cancel Focusable" tabindex="-1">&#x2715; Cancel</button>',
            '</div>'
        ].join('');

        document.body.appendChild(bar);

        var toggleBtn = bar.querySelector('.sls-btn-toggle');
        var cancelBtn = bar.querySelector('.sls-btn-cancel');

        toggleBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            toggleBtn.disabled = true;
            var currentId = bar.dataset.slsAppId;
            var isCurrentlyPaused = toggleBtn.classList.contains('resume');
            var endpoint = isCurrentlyPaused ? '/resume?id=' : '/pause?id=';
            fetch(BASE_URL + endpoint + currentId).catch(function () {});
            setTimeout(syncDownloads, 200);
        };

        cancelBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            cancelBtn.disabled = true;
            var currentId = bar.dataset.slsAppId;
            fetch(BASE_URL + '/cancel?id=' + currentId).catch(function () {});
            setTimeout(syncDownloads, 300);
        };

        [toggleBtn, cancelBtn].forEach(function (b) {
            registerSteamFocusNode(b);
            b.addEventListener('vgp_onfocus', function () { b.style.outline = '2px solid white'; b.style.outlineOffset = '2px'; });
            b.addEventListener('vgp_onblur',  function () { b.style.outline = 'none'; });
            b.addEventListener('vgp_onok',    function (e) { e.preventDefault(); e.stopPropagation(); b.click(); });
        });

        return bar;
    }

    function updateFloatingBar(primaryDl) {
        var floatingEl = document.getElementById('sls-floating-dl-bar');
        if (!primaryDl) {
            if (floatingEl) {
                floatingEl.classList.add('hiding');
                setTimeout(function () {
                    if (floatingEl && floatingEl.parentNode) floatingEl.parentNode.removeChild(floatingEl);
                }, 260);
            }
            return;
        }

        floatingEl = ensureFloatingBar(primaryDl.appId);
        floatingEl.classList.remove('hiding');
        floatingEl.dataset.slsAppId = primaryDl.appId;

        var pct      = Math.max(0, Math.min(100, primaryDl.percent || 0));
        var phase    = primaryDl.phase || 'downloading';
        var isPaused = phase === 'paused' || primaryDl.paused;
        var name     = primaryDl.gameName || ('App ' + primaryDl.appId);
        var label    = PHASE_LABELS[phase] || phase;
        var speedStr = (phase === 'downloading' && !isPaused) ? formatSpeed(primaryDl.speedBps) : '';
        var etaStr   = (phase === 'downloading' && !isPaused && primaryDl.etaSec >= 0) ? formatEta(primaryDl.etaSec) : '';

        // 1. Icon
        var iconWrap = floatingEl.querySelector('.sls-floating-icon-wrap');
        if (iconWrap && floatingEl.dataset.lastPhase !== phase) {
            iconWrap.innerHTML = spinnerSVG(phase, 22);
            floatingEl.dataset.lastPhase = phase;
        }

        // 2. Title & Percentage
        var titleEl = floatingEl.querySelector('.sls-floating-title');
        if (titleEl && titleEl.textContent !== name) {
            titleEl.textContent = name;
            titleEl.title = name;
        }

        var pctEl = floatingEl.querySelector('.sls-floating-pct');
        var pctText = pct + '%';
        if (pctEl && pctEl.textContent !== pctText) {
            pctEl.textContent = pctText;
        }

        // 3. Track Fill
        var fillEl = floatingEl.querySelector('.sls-dl-fill');
        if (fillEl) {
            var fillClass = (phase === 'failed') ? 'sls-dl-fill failed'
                          : (phase === 'done')   ? 'sls-dl-fill done'
                          : isPaused             ? 'sls-dl-fill paused'
                          :                        'sls-dl-fill';
            if (fillEl.className !== fillClass) fillEl.className = fillClass;
            fillEl.style.width = pct + '%';
        }

        // 4. Sub labels
        var phaseEl = floatingEl.querySelector('.sls-floating-phase');
        if (phaseEl) {
            var phaseText = label + (primaryDl.depotsTotal > 0 ? ' (' + (primaryDl.depotsDone || 0) + '/' + primaryDl.depotsTotal + ')' : '');
            if (phaseEl.textContent !== phaseText) phaseEl.textContent = phaseText;
        }

        var metaEl = floatingEl.querySelector('.sls-floating-meta');
        if (metaEl) {
            var metaText = (speedStr ? speedStr + ' ' : '') + (etaStr ? '~' + etaStr : '');
            if (metaEl.textContent !== metaText) metaEl.textContent = metaText;
        }

        // 5. Controls updated in-place!
        var toggleBtn = floatingEl.querySelector('.sls-btn-toggle');
        if (toggleBtn) {
            if (phase === 'done' || phase === 'failed') {
                toggleBtn.style.display = 'none';
            } else {
                toggleBtn.style.display = 'inline-block';
                toggleBtn.disabled = false;
                if (isPaused) {
                    if (!toggleBtn.classList.contains('resume')) {
                        toggleBtn.innerHTML = '&#x25B6; Resume';
                        toggleBtn.className = 'sls-dl-btn sls-btn-toggle resume Focusable';
                        toggleBtn.title = 'Resume download';
                    }
                } else {
                    if (!toggleBtn.classList.contains('pause')) {
                        toggleBtn.innerHTML = '&#x23F8; Pause';
                        toggleBtn.className = 'sls-dl-btn sls-btn-toggle pause Focusable';
                        toggleBtn.title = 'Pause download';
                    }
                }
            }
        }

        var cancelBtn = floatingEl.querySelector('.sls-btn-cancel');
        if (cancelBtn) {
            if (phase === 'done' || phase === 'failed') {
                cancelBtn.style.display = 'none';
            } else {
                cancelBtn.style.display = 'inline-block';
                cancelBtn.disabled = false;
            }
        }
    }

    // ── State & Polling ───────────────────────────────────────────────────────

    var activeDownloads = {}; // appid → downloadData
    var notDownloading  = {}; // appid → timestamp (cooldown)
    var pollTimer       = null;
    var lastInteractionTime = Date.now();

    function updateInteraction() {
        lastInteractionTime = Date.now();
        notDownloading = {}; // Clear suppression on user interaction
        debouncedSync();
    }

    ['mousedown', 'keydown', 'pointerdown', 'touchstart'].forEach(function(evt) {
        document.addEventListener(evt, updateInteraction, { capture: true, passive: true });
    });
    window.addEventListener('vgp_onok', updateInteraction, true);
    window.addEventListener('vgp_ondirection', updateInteraction, true);

    // Cross-script notification
    window.addEventListener('sls-download-started', function (e) {
        var id = (e && e.detail && e.detail.appid) ? String(e.detail.appid) : null;
        if (id) {
            delete notDownloading[id];
            delete activeDownloads[id];
        }
        updateInteraction();
        setTimeout(syncDownloads, 100);
        setTimeout(syncDownloads, 400);
        setTimeout(syncDownloads, 1200);
    });

    window.addEventListener('message', function (e) {
        if (e && e.data && e.data.type === 'sls-download-started') {
            var id = String(e.data.appid);
            delete notDownloading[id];
            updateInteraction();
            setTimeout(syncDownloads, 100);
            setTimeout(syncDownloads, 400);
        }
    });

    // ── Main Synchronization Logic ────────────────────────────────────────────

    var syncInProgress = false;

    function syncDownloads() {
        if (syncInProgress) return;
        syncInProgress = true;

        fetch(BASE_URL + '/active-downloads')
            .then(function (r) {
                if (!r.ok) throw new Error('Endpoint not available');
                return r.json();
            })
            .then(function (dlList) {
                processActiveDownloads(Array.isArray(dlList) ? dlList : []);
            })
            .catch(function () {
                // Fallback for older binary or when /active-downloads is unavailable
                var row = findActionRow();
                var currentAppId = row ? extractAppId(row) : null;
                if (!currentAppId) {
                    processActiveDownloads([]);
                    return;
                }

                var now = Date.now();
                var recentlyInteracted = (now - lastInteractionTime) < 4000;
                if (!recentlyInteracted && notDownloading[currentAppId] && (now - notDownloading[currentAppId]) < 4000) {
                    processActiveDownloads([]);
                    return;
                }

                fetch(BASE_URL + '/check?id=' + currentAppId)
                    .then(function (r) { return r.json(); })
                    .then(function (checkData) {
                        if (checkData.downloading) {
                            fetch(BASE_URL + '/progress?id=' + currentAppId)
                                .then(function (pr) { return pr.json(); })
                                .then(function (progData) {
                                    progData.appId = currentAppId;
                                    progData.paused = checkData.paused || (progData.phase === 'paused');
                                    processActiveDownloads([progData]);
                                })
                                .catch(function () { processActiveDownloads([]); });
                        } else {
                            notDownloading[currentAppId] = Date.now();
                            processActiveDownloads([]);
                        }
                    })
                    .catch(function () { processActiveDownloads([]); });
            });
    }

    function processActiveDownloads(downloads) {
        syncInProgress = false;

        var newActiveMap = {};
        downloads.forEach(function (d) {
            if (d && d.appId) newActiveMap[String(d.appId)] = d;
        });
        activeDownloads = newActiveMap;

        var actionRow = findActionRow();
        var currentAppId = actionRow ? extractAppId(actionRow) : null;
        var hasVisibleInlineCard = false;

        // 1. Sync Inline Card on Game Details page
        if (actionRow && currentAppId && activeDownloads[currentAppId]) {
            var dl = activeDownloads[currentAppId];
            var card = ensureInlineCard(actionRow, currentAppId);
            updateInlineCard(card, dl);
            hasVisibleInlineCard = true;
        } else {
            // Remove any obsolete inline cards
            document.querySelectorAll('.sls-dl-card').forEach(function (c) {
                var cId = c.dataset.slsAppId;
                if (!cId || !activeDownloads[cId]) {
                    c.style.transition = 'opacity 0.25s ease';
                    c.style.opacity = '0';
                    setTimeout(function () {
                        if (c.parentNode) c.parentNode.removeChild(c);
                    }, 250);
                }
            });
        }

        // 2. Sync Floating Global Progress Indicator
        var downloadIds = Object.keys(activeDownloads);
        if (downloadIds.length > 0 && !hasVisibleInlineCard) {
            updateFloatingBar(activeDownloads[downloadIds[0]]);
        } else {
            updateFloatingBar(null);
        }

        scheduleNextPoll(downloadIds.length > 0);
    }

    function scheduleNextPoll(hasActive) {
        if (pollTimer) clearTimeout(pollTimer);
        var interval = hasActive ? POLL_ACTIVE_MS : POLL_IDLE_MS;
        pollTimer = setTimeout(syncDownloads, interval);
    }

    // ── Debounced Scan on DOM Mutation ────────────────────────────────────────

    var debouncedTimer = null;
    function debouncedSync() {
        if (debouncedTimer) clearTimeout(debouncedTimer);
        debouncedTimer = setTimeout(syncDownloads, 80);
    }

    var observer = new MutationObserver(debouncedSync);
    observer.observe(document.body, { childList: true, subtree: true });

    // Initial immediate sync
    syncDownloads();
    setTimeout(syncDownloads, 500);
    setTimeout(syncDownloads, 1500);

})();
