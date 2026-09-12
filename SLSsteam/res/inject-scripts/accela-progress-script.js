// accela-progress-script.js
// Injected into every Steam library page (non-SharedJSContext).
// Polls /progress and /check on port 9001 to show a live download
// progress card for any game being installed via accela-helper.
//
// Tracks PER CONTAINER so both scroll states show the card simultaneously.
// Card is inserted as firstChild of lO1IF132jJ1gc9yz2HYvV — before SLS buttons.
// Guard flag: window.__slsAccelaProgressInjected

(function () {
    'use strict';
    if (window.__slsAccelaProgressInjected) return;
    window.__slsAccelaProgressInjected = true;

    // ── Constants ─────────────────────────────────────────────────────────────

    var POLL_MS  = 1500;
    var BASE_URL = 'http://127.0.0.1:9001';

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

    // ── Shared CSS ────────────────────────────────────────────────────────────

    function ensureStyles() {
        if (document.getElementById('sls-dl-styles')) return;
        var s = document.createElement('style');
        s.id = 'sls-dl-styles';
        s.textContent = [
            '.sls-dl-card{',
            '  display:inline-flex;align-items:center;gap:8px;',
            '  background:linear-gradient(135deg,#1a1d23 0%,#13151a 100%);',
            '  border:1px solid rgba(255,255,255,0.08);border-radius:6px;',
            '  padding:8px 12px;margin-right:8px;min-width:240px;',
            '  box-shadow:0 2px 12px rgba(0,0,0,0.45);',
            '  font-family:"Motiva Sans",Arial,sans-serif;',
            '  vertical-align:middle;',
            '}',
            '.sls-dl-icon{flex-shrink:0;animation:sls-spin 1.4s linear infinite;}',
            '.sls-dl-icon.done{animation:none;}',
            '.sls-dl-icon.paused{animation:none;opacity:0.6;}',
            '@keyframes sls-spin{to{transform:rotate(360deg)}}',
            '.sls-dl-body{display:flex;flex-direction:column;gap:3px;flex:1;min-width:0;}',
            '.sls-dl-header{display:flex;justify-content:space-between;align-items:baseline;gap:4px;}',
            '.sls-dl-game{font-size:12px;font-weight:700;color:#c6d4df;',
            '  white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:110px;}',
            '.sls-dl-pct{font-size:12px;font-weight:700;color:#67c1f5;flex-shrink:0;}',
            '.sls-dl-track{height:4px;background:rgba(255,255,255,0.1);border-radius:2px;overflow:hidden;}',
            '.sls-dl-fill{height:100%;border-radius:2px;',
            '  background:linear-gradient(90deg,#4d9c36,#75b022);',
            '  transition:width 0.6s ease;will-change:width;}',
            '.sls-dl-fill.failed{background:#c0392b;}',
            '.sls-dl-fill.done{background:#4caf50;}',
            '.sls-dl-fill.paused{background:linear-gradient(90deg,#4a5568,#718096);}',
            '.sls-dl-meta{display:flex;justify-content:space-between;align-items:center;gap:4px;}',
            '.sls-dl-phase{font-size:10px;color:#8ba3b8;text-transform:uppercase;letter-spacing:.04em;flex-shrink:0;}',
            '.sls-dl-speed{font-size:10px;color:#67c1f5;flex-shrink:0;}',
            '.sls-dl-eta{font-size:10px;color:#5a7a8a;flex-shrink:0;}',
            // Control buttons row
            '.sls-dl-controls{display:flex;gap:4px;flex-shrink:0;align-items:center;}',
            '.sls-dl-btn{background:rgba(255,255,255,0.06);border:1px solid rgba(255,255,255,0.1);',
            '  color:#9ca3af;cursor:pointer;font-size:11px;line-height:1;padding:3px 6px;',
            '  border-radius:3px;transition:all 0.15s;white-space:nowrap;}',
            '.sls-dl-btn:hover{background:rgba(255,255,255,0.12);color:#fff;}',
            '.sls-dl-btn.pause:hover{background:rgba(250,204,21,0.15);color:#fbbf24;border-color:rgba(251,191,36,0.3);}',
            '.sls-dl-btn.resume:hover{background:rgba(74,222,128,0.15);color:#4ade80;border-color:rgba(74,222,128,0.3);}',
            '.sls-dl-btn.cancel:hover{background:rgba(239,68,68,0.15);color:#f87171;border-color:rgba(239,68,68,0.3);}',
            '.sls-dl-btn:disabled{opacity:0.3;cursor:default;pointer-events:none;}',
        ].join('');
        document.head.appendChild(s);
    }

    // ── SVG helpers ───────────────────────────────────────────────────────────

    function spinnerSVG(phase) {
        var isDone   = phase === 'done';
        var isFailed = phase === 'failed';
        var isPaused = phase === 'paused';

        if (isDone)
            return '<svg class="sls-dl-icon done" width="18" height="18" viewBox="0 0 24 24"'
                 + ' fill="none" stroke="#4caf50" stroke-width="2.5"'
                 + ' stroke-linecap="round" stroke-linejoin="round">'
                 + '<polyline points="20 6 9 17 4 12"/></svg>';
        if (isFailed)
            return '<svg class="sls-dl-icon done" width="18" height="18" viewBox="0 0 24 24"'
                 + ' fill="none" stroke="#e74c3c" stroke-width="2.5"'
                 + ' stroke-linecap="round" stroke-linejoin="round">'
                 + '<line x1="18" y1="6" x2="6" y2="18"/>'
                 + '<line x1="6" y1="6" x2="18" y2="18"/></svg>';
        if (isPaused)
            return '<svg class="sls-dl-icon paused" width="18" height="18" viewBox="0 0 24 24"'
                 + ' fill="none" stroke="#718096" stroke-width="2.5"'
                 + ' stroke-linecap="round" stroke-linejoin="round">'
                 + '<rect x="6" y="4" width="4" height="16"/>'
                 + '<rect x="14" y="4" width="4" height="16"/></svg>';
        return '<svg class="sls-dl-icon" width="18" height="18" viewBox="0 0 24 24"'
             + ' fill="none" stroke="#67c1f5" stroke-width="2.5"'
             + ' stroke-linecap="round" stroke-linejoin="round">'
             + '<path d="M12 2a10 10 0 0 1 10 10" opacity=".3"/>'
             + '<path d="M12 2a10 10 0 0 0-10 10 10 10 0 0 0 10 10"/>'
             + '</svg>';
    }

    function escHtml(s) {
        return String(s)
            .replace(/&/g, '&amp;').replace(/</g, '&lt;')
            .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    // ── Formatting helpers ────────────────────────────────────────────────────

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

    // ── Card HTML ─────────────────────────────────────────────────────────────

    function buildCardHTML(gameName, pct, done, total, phase, speedBps, etaSec) {
        var isDone    = phase === 'done';
        var isFailed  = phase === 'failed';
        var isPaused  = phase === 'paused';
        var isActive  = !isDone && !isFailed;

        var fillClass = isFailed ? 'sls-dl-fill failed'
                      : isDone   ? 'sls-dl-fill done'
                      : isPaused ? 'sls-dl-fill paused'
                      :            'sls-dl-fill';

        var label = PHASE_LABELS[phase] || phase;

        // Speed / ETA — only shown while actively downloading
        var speedStr = (phase === 'downloading') ? formatSpeed(speedBps) : '';
        var etaStr   = (phase === 'downloading' && etaSec >= 0) ? formatEta(etaSec) : '';

        // Controls: pause/resume + cancel, only while download is in-flight
        var controls = '';
        if (isActive && !isDone && !isFailed) {
            if (isPaused) {
                controls = '<div class="sls-dl-controls">'
                         + '<button class="sls-dl-btn resume" data-sls-resume="1" title="Resume download">&#x25B6; Resume</button>'
                         + '<button class="sls-dl-btn cancel" data-sls-cancel="1" title="Cancel download">&#x2715; Cancel</button>'
                         + '</div>';
            } else {
                controls = '<div class="sls-dl-controls">'
                         + '<button class="sls-dl-btn pause" data-sls-pause="1" title="Pause download">&#x23F8; Pause</button>'
                         + '<button class="sls-dl-btn cancel" data-sls-cancel="1" title="Cancel download">&#x2715; Cancel</button>'
                         + '</div>';
            }
        }

        return spinnerSVG(phase)
             + '<div class="sls-dl-body">'
             +   '<div class="sls-dl-header">'
             +     '<span class="sls-dl-game" title="' + escHtml(gameName) + '">' + escHtml(gameName) + '</span>'
             +     '<span class="sls-dl-pct">' + pct + '%</span>'
             +   '</div>'
             +   '<div class="sls-dl-track"><div class="' + fillClass + '" style="width:' + pct + '%"></div></div>'
             +   '<div class="sls-dl-meta">'
             +     '<span class="sls-dl-phase">' + label + (total > 0 ? ' · ' + done + '/' + total : '') + '</span>'
             +     (speedStr ? '<span class="sls-dl-speed">' + speedStr + '</span>' : '')
             +     (etaStr   ? '<span class="sls-dl-eta">~' + etaStr + '</span>' : '')
             +   '</div>'
             + '</div>'
             + controls;
    }

    function applyData(cardEl, data) {
        var pct      = Math.max(0, Math.min(100, data.percent || 0));
        var phase    = data.phase || 'idle';
        var name     = data.gameName || cardEl.dataset.slsGameName || ('AppID ' + cardEl.dataset.slsAppId);
        if (data.gameName) cardEl.dataset.slsGameName = data.gameName;

        cardEl.innerHTML = buildCardHTML(
            name, pct,
            data.depotsDone || 0,
            data.depotsTotal || 0,
            phase,
            data.speedBps || 0,
            data.etaSec != null ? data.etaSec : -1
        );

        var appid = cardEl.dataset.slsAppId;

        // Wire pause button
        var pauseBtn = cardEl.querySelector('[data-sls-pause]');
        if (pauseBtn) {
            pauseBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                pauseBtn.disabled = true;
                fetch(BASE_URL + '/pause?id=' + appid).catch(function() {});
            });
        }

        // Wire resume button
        var resumeBtn = cardEl.querySelector('[data-sls-resume]');
        if (resumeBtn) {
            resumeBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                resumeBtn.disabled = true;
                fetch(BASE_URL + '/resume?id=' + appid).catch(function() {});
            });
        }

        // Wire cancel button
        var cancelBtn = cardEl.querySelector('[data-sls-cancel]');
        if (cancelBtn) {
            cancelBtn.addEventListener('click', function(e) {
                e.stopPropagation();
                cancelBtn.disabled = true;
                fetch(BASE_URL + '/cancel?id=' + appid).catch(function() {});
            });
        }
    }

    // ── Per-container tracking ────────────────────────────────────────────────
    // Key: a random string stamped on each lO1IF132jJ1gc9yz2HYvV node
    // Value: { appid, cardEl, timerId, failCount, done }

    var tracked = {};   // containerKey → state
    var inFlight = {};  // containerKey → true (pending /check)
    var recentlyChecked = {};  // appid → timestamp (to prevent spamming /check after download ends)
    var notDownloading = {};   // appid → true  (confirmed not downloading; skip until page reload)

    function containerKey(node) {
        if (!node.dataset.slsDlKey)
            node.dataset.slsDlKey = Math.random().toString(36).slice(2);
        return node.dataset.slsDlKey;
    }

    // Insert / ensure card is firstChild of buttonRow
    function ensureCard(buttonRow, appid, gameName) {
        var key  = containerKey(buttonRow);
        var info = tracked[key];

        // Card element already exists in this container
        if (info && info.cardEl && info.cardEl.parentNode === buttonRow)
            return info.cardEl;

        ensureStyles();
        var card = document.createElement('div');
        card.className         = 'sls-dl-card';
        card.dataset.slsAppId  = appid;
        card.dataset.slsGameName = gameName || '';
        card.innerHTML = buildCardHTML(gameName || ('AppID ' + appid), 0, 0, 0, 'starting', 0, -1);

        // Insert FIRST — before all SLS buttons and Steam buttons
        buttonRow.insertBefore(card, buttonRow.firstChild);

        if (!tracked[key]) tracked[key] = { appid: appid, cardEl: card, timerId: null, failCount: 0, done: false };
        else tracked[key].cardEl = card;

        return card;
    }

    function removeCardsForAppid(appid) {
        // Remove card from every tracked container for this appid
        Object.keys(tracked).forEach(function (key) {
            var info = tracked[key];
            if (info.appid !== appid) return;
            if (info.timerId) clearTimeout(info.timerId);
            if (info.cardEl && info.cardEl.parentNode) {
                info.cardEl.style.transition = 'opacity 0.4s';
                info.cardEl.style.opacity    = '0';
                var el = info.cardEl;
                setTimeout(function () { if (el.parentNode) el.parentNode.removeChild(el); }, 420);
            }
            delete tracked[key];
        });
    }

    // ── Poll loop (shared — one timer per appid, updates all containers) ──────

    var pollTimers = {};  // appid → timer id

    function schedulePoll(appid) {
        if (pollTimers[appid]) return;
        pollTimers[appid] = setTimeout(function () {
            delete pollTimers[appid];
            doPoll(appid);
        }, POLL_MS);
    }

    function doPoll(appid) {
        // Abort if no containers are still tracking this appid
        var anyAlive = Object.keys(tracked).some(function (k) {
            return tracked[k].appid === appid;
        });
        if (!anyAlive) return;

        fetch(BASE_URL + '/progress?id=' + appid)
            .then(function (r) { return r.json(); })
            .then(function (data) {
                var isDone   = data.phase === 'done';
                var isFailed = data.phase === 'failed';

                // Update every live card for this appid
                Object.keys(tracked).forEach(function (key) {
                    var info = tracked[key];
                    if (info.appid !== appid) return;
                    info.failCount = 0;
                    // Re-ensure card is still in its container (handles React re-renders)
                    if (!info.cardEl || !info.cardEl.parentNode) {
                        var row = document.querySelector('[data-sls-dl-key="' + key + '"]');
                        if (row) info.cardEl = ensureCard(row, appid, data.gameName || '');
                    }
                    if (info.cardEl) applyData(info.cardEl, data);
                });

                if (isDone || isFailed) {
                    // Mark this appid as recently checked — don't spam /check for 60s
                    recentlyChecked[appid] = Date.now();
                    delete notDownloading[appid];
                    setTimeout(function () {
                        removeCardsForAppid(appid);
                        // Reset app-details injection flag so buttons re-appear
                        document.querySelectorAll('[data-sls-injected="' + appid + '"]')
                            .forEach(function (el) { delete el.dataset.slsInjected; });
                    }, 2500);
                    return;
                }

                schedulePoll(appid);
            })
            .catch(function () {
                var anyAlive2 = false;
                Object.keys(tracked).forEach(function (key) {
                    if (tracked[key].appid !== appid) return;
                    tracked[key].failCount = (tracked[key].failCount || 0) + 1;
                    if (tracked[key].failCount < 10) anyAlive2 = true;
                    else {
                        if (tracked[key].cardEl && tracked[key].cardEl.parentNode)
                            tracked[key].cardEl.parentNode.removeChild(tracked[key].cardEl);
                        delete tracked[key];
                    }
                });
                if (anyAlive2) schedulePoll(appid);
            });
    }

    // ── DOM scanner ───────────────────────────────────────────────────────────

    function extractAppId(manageBtn) {
        var appid = null;
        var curr  = manageBtn;
        while (curr && curr !== document.body) {
            var cls = (typeof curr.className === 'string') ? curr.className : '';
            var m = cls.match(/\bapp_([0-9]+)\b/);
            if (m) { appid = m[1]; break; }
            var da = curr.getAttribute('data-appid');
            if (da) { appid = da; break; }
            curr = curr.parentElement;
        }
        if (!appid) {
            try {
                curr = manageBtn;
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
        }
        return appid;
    }

    function scanForDownloads() {
        document.querySelectorAll('div[aria-label="Manage"]').forEach(function (manageBtn) {
            var manageContainer = manageBtn.parentNode;
            if (!manageContainer) return;

            // buttonRow = lO1IF132jJ1gc9yz2HYvV
            var buttonRow = manageContainer.parentNode;
            if (!buttonRow) return;

            var appid = extractAppId(manageBtn);
            if (!appid) return;

            // Must declare now before any use of it below.
            var now = Date.now();

            // Skip if confirmed not-downloading, but only for a short window (15s).
            // Long enough to avoid hammering /check on every 3s tick, but short
            // enough that a download starting shortly after is still caught quickly.
            // If the user interacted recently (< 3s), bypass this cache to give instant feedback.
            var recentlyInteracted = (now - lastInteractionTime) < 3000;
            if (!recentlyInteracted && notDownloading[appid] && (now - notDownloading[appid]) < 15000) return;

            // Skip if we recently checked this appid after a completed/failed download
            // (60s cooldown set by doPoll when isDone/isFailed).
            if (recentlyChecked[appid] && (now - recentlyChecked[appid]) < 60000) {
                return;
            }

            var key = containerKey(buttonRow);

            // Already tracking this container for this appid
            if (tracked[key] && tracked[key].appid === appid) {
                // Ensure card is still firstChild (React may have re-rendered)
                var info = tracked[key];
                if (!info.cardEl || !info.cardEl.parentNode) {
                    info.cardEl = ensureCard(buttonRow, appid, info.gameName || '');
                } else if (info.cardEl.parentNode === buttonRow &&
                           buttonRow.firstChild !== info.cardEl) {
                    // Card drifted — move back to front
                    buttonRow.insertBefore(info.cardEl, buttonRow.firstChild);
                }
                return;
            }

            // Skip if a /check is in-flight for this container
            if (inFlight[key]) return;
            inFlight[key] = true;

            fetch(BASE_URL + '/check?id=' + appid)
                .then(function (r) { return r.json(); })
                .then(function (data) {
                    delete inFlight[key];
                    if (!data.downloading) {
                        // Suppress re-checks for 15s when confirmed idle.
                        notDownloading[appid] = Date.now();
                        return;
                    }
                    // Download is active — clear the idle suppression.
                    delete notDownloading[appid];

                    // Create card in this container
                    var card = ensureCard(buttonRow, appid, '');
                    tracked[key] = { appid: appid, cardEl: card, timerId: null, failCount: 0, done: false };

                    // Fetch initial progress for game name + speed/ETA
                    fetch(BASE_URL + '/progress?id=' + appid)
                        .then(function (r2) { return r2.json(); })
                        .then(function (d2) {
                            if (tracked[key]) {
                                tracked[key].gameName = d2.gameName || '';
                                applyData(card, d2);
                            }
                        })
                        .catch(function () {});

                    schedulePoll(appid);
                })
                .catch(function () { delete inFlight[key]; });
        });
    }

    // ── Boot ──────────────────────────────────────────────────────────────────

    var scanRaf = null;
    function debouncedScan() {
        if (scanRaf) return;
        scanRaf = requestAnimationFrame(function () { scanRaf = null; scanForDownloads(); });
    }

    var lastInteractionTime = 0;
    document.addEventListener('mousedown', function () {
        lastInteractionTime = Date.now();
        // The backend takes a few milliseconds to write the JSON after the Steam Install button is clicked.
        // We schedule a few staggered checks to ensure we catch it immediately for instant UI feedback.
        setTimeout(debouncedScan, 100);
        setTimeout(debouncedScan, 500);
        setTimeout(debouncedScan, 1500);
    }, { capture: true, passive: true });

    new MutationObserver(debouncedScan).observe(document.body, { childList: true, subtree: true });
    scanForDownloads();
    setInterval(scanForDownloads, 3000);

})();
