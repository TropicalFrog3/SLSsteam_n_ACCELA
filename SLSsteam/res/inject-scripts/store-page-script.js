(function() {
    if (window.__slsLuaBtnAdded) return;
    window.__slsLuaBtnAdded = true;
    console.log('[SLS] Store Page Script Triggered');

    function ping(msg) { console.log('[SLS] StorePage: ' + msg); }

    ping('Script active on: ' + window.location.href);

    var observer = null;
    var debounceTimer = null;

    // Cache of app unlock status: { appid: { exists, pending, onlineFixInstalled, autoCrackInstalled } }
    var appUnlockStatus = {};

    var luaProviders = [
        { name: 'Ryuu', key: 'sls-ryuu-key', url: 'https://generator.ryuu.lol' },
        { name: 'DepotBox', key: 'sls-dpbx-key', url: 'https://depotbox.org/' },
        { name: 'HubcapDB', key: 'sls-hubcap-key', url: 'https://hubcapmanifest.com/api-keys/stats' },
        { name: 'Morrenus', key: 'sls-morr-key', url: '#' },
        { name: 'Sushi', key: 'sls-sushi-key', url: '#' },
        { name: 'Spinoza', key: 'sls-spinoza-key', url: '#' },
        { name: 'TwentyTwo Cloud', key: 'sls-twentytwo-key', url: '#' }
    ];

    function getProviderOrder() {
        var saved = (localStorage.getItem('sls-lua-provider-order') || '').split(',');
        var order = [];
        saved.forEach(function(value) {
            var index = parseInt(value, 10);
            if (index >= 0 && index < luaProviders.length && order.indexOf(index) === -1) order.push(index);
        });
        for (var i = 0; i < luaProviders.length; i++) {
            if (order.indexOf(i) === -1) order.push(i);
        }
        return order;
    }

    function saveProviderOrder(order) {
        localStorage.setItem('sls-lua-provider-order', order.join(','));
    }

    function escapeHtml(value) {
        return String(value).replace(/[&<>"']/g, function(character) {
            return {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[character];
        });
    }

    function sendApiKeys() {
        var morr = localStorage.getItem('sls-morr-key') || '%MORR_KEY%';
        var ryuu = localStorage.getItem('sls-ryuu-key') || '%RYUU_KEY%';
        var dpbx = localStorage.getItem('sls-dpbx-key') || '%DPBX_KEY%';
        var hubcap = localStorage.getItem('sls-hubcap-key') || '%HUBCAP_KEY%';
        window.location.hash = 'sls-auth-MORR=' + encodeURIComponent(morr) + '&RYUU=' + encodeURIComponent(ryuu) + '&DPBX=' + encodeURIComponent(dpbx) + '&HUBCAP=' + encodeURIComponent(hubcap) + '-TS=' + Date.now();
    }

    function sendLuaRequest(appid, providerIndex, customOrder) {
        var stamp = Date.now();
        if (providerIndex === null) {
            var order = customOrder || getProviderOrder();
            window.location.hash = 'sls-click-auto-' + appid + '-' + order.join(',') + '-' + stamp;
        } else {
            window.location.hash = 'sls-click-provider-' + providerIndex + '-' + appid + '-' + stamp;
        }
    }

    // ── Row loading bar ───────────────────────────────────────────────────
    // Phases reported via pushStatus (button span text) → progress milestones.
    // Between milestones the bar crawls automatically; it snaps forward when the
    // next phase text is detected. On success it completes to 100% then fades
    // out; on failure it turns red and fades out after a short pause.

    var SLS_PHASE_MAP = [
        { match: /checking/i,    pct: 5  },
        { match: /trying/i,      pct: 35 },
        { match: /browser/i,     pct: 45 },
        { match: /extracting/i,  pct: 65 },
        { match: /installing/i,  pct: 82 },
        { match: /installed/i,   pct: 100 },
    ];
    var SLS_FAIL_TEXTS = /not available|failed|not found|steam not|no api/i;

    function injectLoadingBarStyles() {
        if (document.getElementById('sls-bar-style')) return;
        var s = document.createElement('style');
        s.id = 'sls-bar-style';
        s.textContent = [
            '@keyframes slsShimmer {',
            '  0% { background-position: -200% 0; }',
            '  100% { background-position: 200% 0; }',
            '}',
            '.sls-bar-host { position:relative; overflow:hidden; }',
            '.sls-bar-fill {',
            '  position:absolute; top:0; left:0; height:100%;',
            '  width:0%; border-radius:inherit;',
            '  background: linear-gradient(90deg, rgba(16, 185, 129, 0.15) 0%, rgba(52, 211, 153, 0.35) 50%, rgba(16, 185, 129, 0.15) 100%),',
            '              linear-gradient(90deg, rgba(255,255,255,0) 0%, rgba(255,255,255,0.2) 50%, rgba(255,255,255,0) 100%);',
            '  background-size: 100% 100%, 200% 100%;',
            '  animation: slsShimmer 2.5s infinite linear;',
            '  box-shadow: inset 0 0 12px rgba(52, 211, 153, 0.3), 0 0 8px rgba(16, 185, 129, 0.4);',
            '  border-right: 2px solid rgba(52, 211, 153, 0.8);',
            '  transition: width 0.55s cubic-bezier(0.4,0,0.2,1), background 0.4s ease, box-shadow 0.4s ease, border-color 0.4s ease;',
            '  pointer-events:none; z-index:0;',
            '}',
            '.sls-bar-fill.sls-bar-fail {',
            '  background: linear-gradient(90deg, rgba(225, 29, 72, 0.2) 0%, rgba(244, 63, 94, 0.4) 50%, rgba(225, 29, 72, 0.2) 100%),',
            '              linear-gradient(90deg, rgba(255,255,255,0) 0%, rgba(255,255,255,0.15) 50%, rgba(255,255,255,0) 100%);',
            '  background-size: 100% 100%, 200% 100%;',
            '  box-shadow: inset 0 0 12px rgba(244, 63, 94, 0.35), 0 0 10px rgba(225, 29, 72, 0.5);',
            '  border-right: 2px solid rgba(244, 63, 94, 0.9);',
            '}',
            '.sls-bar-fill.sls-bar-done {',
            '  background: linear-gradient(90deg, rgba(16, 185, 129, 0.35) 0%, rgba(52, 211, 153, 0.55) 100%);',
            '  box-shadow: inset 0 0 16px rgba(52, 211, 153, 0.5), 0 0 12px rgba(16, 185, 129, 0.6);',
            '  border-right: 2px solid rgba(52, 211, 153, 1);',
            '}',
            '.sls-bar-host > a { position:relative; z-index:1; }',
        ].join('\n');
        document.head.appendChild(s);
    }

    /**
     * Attach a loading bar to `luaLink`'s parent wrapper and poll the span
     * text for phase updates until the download completes or fails.
     * Works identically for single-provider and auto-download.
     */
    function startRowLoadingBar(luaBtn, luaLink) {
        injectLoadingBarStyles();

        // Find the actual clickable <a> element's parent to host the bar
        var host = luaLink.parentElement || luaBtn;
        host.classList.add('sls-bar-host');

        // Remove any stale bar
        var oldBar = host.querySelector('.sls-bar-fill');
        if (oldBar) oldBar.remove();

        var bar = document.createElement('div');
        bar.className = 'sls-bar-fill';
        host.insertBefore(bar, host.firstChild);

        var currentPct  = 0;
        var targetPct   = 0;
        var pollId      = null;
        var lastText    = '';
        var finished    = false;

        function setTarget(pct) {
            if (pct <= targetPct) return;
            targetPct = pct;
            bar.style.width = targetPct + '%';
        }

        function finish(success) {
            if (finished) return;
            finished = true;
            clearInterval(pollId);
            if (success) {
                bar.classList.add('sls-bar-done');
                bar.style.width = '100%';
                setTimeout(function() {
                    bar.style.transition = 'opacity 0.6s ease';
                    bar.style.opacity = '0';
                    setTimeout(function() {
                        bar.remove();
                        host.classList.remove('sls-bar-host');
                    }, 650);
                }, 900);
            } else {
                bar.classList.add('sls-bar-fail');
                bar.style.width = '100%';
                setTimeout(function() {
                    bar.style.transition = 'opacity 0.8s ease';
                    bar.style.opacity = '0';
                    setTimeout(function() {
                        bar.remove();
                        host.classList.remove('sls-bar-host');
                    }, 850);
                }, 2200);
            }
        }

        // Crawl the bar a tiny bit every tick so it always looks alive
        function crawl() {
            if (finished) return;
            currentPct = parseFloat(bar.style.width) || 0;
            // Slow organic crawl up to targetPct — max 0.18% per tick
            if (currentPct < targetPct) {
                var step = Math.max(0.05, (targetPct - currentPct) * 0.04);
                currentPct = Math.min(targetPct, currentPct + step);
                bar.style.width = currentPct + '%';
            }
        }
        var crawlId = setInterval(crawl, 80);

        // Start at 2% immediately so the bar is visible right away
        setTarget(2);

        pollId = setInterval(function() {
            var span = luaLink.querySelector('span');
            var text = span ? span.innerText : '';
            if (text === lastText) return;
            lastText = text;

            if (SLS_FAIL_TEXTS.test(text)) {
                clearInterval(crawlId);
                finish(false);
                return;
            }
            if (/installed!?/i.test(text)) {
                clearInterval(crawlId);
                finish(true);
                return;
            }
            for (var i = 0; i < SLS_PHASE_MAP.length; i++) {
                if (SLS_PHASE_MAP[i].match.test(text)) {
                    setTarget(SLS_PHASE_MAP[i].pct);
                    break;
                }
            }
        }, 150);
    }

    var globalDownloadSessions = {};

    function saveSessionToStorage(appid) {
        var session = globalDownloadSessions[appid];
        if (!session) return;
        try {
            var dataToSave = {
                requestedProviders: session.requestedProviders,
                providerStates: session.providerStates,
                allowedIndices: session.allowedIndices,
                currentActiveIdx: session.currentActiveIdx,
                targetPct: session.targetPct,
                finished: session.finished
            };
            localStorage.setItem('sls-session-' + appid, JSON.stringify(dataToSave));
        } catch(e) {}
    }

    function loadSessionFromStorage(appid) {
        try {
            var raw = localStorage.getItem('sls-session-' + appid);
            if (!raw) return null;
            var data = JSON.parse(raw);
            return data;
        } catch(e) {
            return null;
        }
    }

    function getOrCreateGlobalSession(appid) {
        if (!globalDownloadSessions[appid]) {
            var saved = loadSessionFromStorage(appid);
            if (saved) {
                globalDownloadSessions[appid] = {
                    requestedProviders: saved.requestedProviders || {},
                    providerStates: saved.providerStates || {},
                    allowedIndices: saved.allowedIndices || [],
                    currentActiveIdx: typeof saved.currentActiveIdx === 'number' ? saved.currentActiveIdx : -1,
                    targetPct: saved.targetPct || 0,
                    finished: !!saved.finished,
                    lastText: '',
                    pollId: null,
                    crawlId: null
                };
            } else {
                globalDownloadSessions[appid] = {
                    requestedProviders: {},
                    providerStates: {},
                    allowedIndices: [],
                    currentActiveIdx: -1,
                    targetPct: 0,
                    finished: false,
                    lastText: '',
                    pollId: null,
                    crawlId: null
                };
            }
        }
        return globalDownloadSessions[appid];
    }

    function syncModalUI(appid) {
        var overlay = document.getElementById('sls-overlay-modal');
        if (!overlay || overlay.dataset.slsAppid !== String(appid)) return;
        var session = globalDownloadSessions[appid];
        if (!session) return;

        Object.keys(session.requestedProviders).forEach(function(pIdxStr) {
            var pIdx = parseInt(pIdxStr, 10);
            var btn = overlay.querySelector('[data-sls-provider-download="' + pIdx + '"]');
            if (btn) {
                // btn.disabled = true;
                // btn.style.opacity = '0.4';
                // btn.style.cursor = 'not-allowed';
            }
        });

        Object.keys(session.providerStates).forEach(function(pIdxStr) {
            var pIdx = parseInt(pIdxStr, 10);
            var state = session.providerStates[pIdxStr];
            var rowEl = overlay.querySelector('[data-sls-provider="' + pIdx + '"]');
            if (!rowEl) return;
            var bar = rowEl.querySelector('.sls-bar-fill');
            if (!bar) {
                injectLoadingBarStyles();
                bar = document.createElement('div');
                bar.className = 'sls-bar-fill';
                rowEl.insertBefore(bar, rowEl.firstChild);
            }
            bar.style.width = state.pct + '%';
            if (state.status === 'done') {
                bar.classList.remove('sls-bar-fail');
                bar.classList.add('sls-bar-done');
            } else if (state.status === 'fail') {
                bar.classList.remove('sls-bar-done');
                bar.classList.add('sls-bar-fail');
            } else {
                bar.classList.remove('sls-bar-done', 'sls-bar-fail');
            }
        });
    }

    function findProviderIndexFromText(text) {
        if (!text) return -1;
        var lower = text.toLowerCase();
        for (var i = 0; i < luaProviders.length; i++) {
            var keyName = luaProviders[i].name.split(' ')[0].toLowerCase();
            if (lower.indexOf(keyName) !== -1) {
                return i;
            }
        }
        return -1;
    }

    function startGlobalDownloadTracking(appid, newAllowedIndices) {
        injectLoadingBarStyles();
        var session = getOrCreateGlobalSession(appid);
        session.allowedIndices = newAllowedIndices || session.allowedIndices;

        if (newAllowedIndices) {
            newAllowedIndices.forEach(function(pIdx) {
                session.requestedProviders[pIdx] = true;
            });
        }

        if (session.currentActiveIdx === -1 || session.finished) {
            if (session.allowedIndices && session.allowedIndices.length > 0) {
                session.currentActiveIdx = session.allowedIndices[0];
            }
            session.targetPct = 5;
            session.finished = false;
            if (session.currentActiveIdx !== -1) {
                session.providerStates[session.currentActiveIdx] = { status: 'active', pct: 5 };
            }
        }

        saveSessionToStorage(appid);
        syncModalUI(appid);

        if (session.crawlId) clearInterval(session.crawlId);
        session.crawlId = setInterval(function() {
            if (session.finished || session.currentActiveIdx === -1) return;
            var st = session.providerStates[session.currentActiveIdx];
            if (!st) return;
            if (st.pct < session.targetPct) {
                var step = Math.max(0.05, (session.targetPct - st.pct) * 0.04);
                st.pct = Math.min(session.targetPct, st.pct + step);
                saveSessionToStorage(appid);
                syncModalUI(appid);
            }
        }, 80);

        if (session.pollId) clearInterval(session.pollId);
        session.pollId = setInterval(function() {
            var pageBtn = document.querySelector('.sls-lua-btn[data-sls-appid="' + appid + '"]');
            var text = pageBtn ? (pageBtn.dataset.slsStatus || (pageBtn.querySelector('a span') ? pageBtn.querySelector('a span').innerText : '')) : '';
            if (text === session.lastText) return;
            session.lastText = text;

            if (SLS_FAIL_TEXTS.test(text)) {
                finishGlobalTracking(appid, false);
                return;
            }
            if (/installed!?/i.test(text)) {
                finishGlobalTracking(appid, true);
                return;
            }

            var allowed = session.allowedIndices || [];
            var foundIdx = findProviderIndexFromText(text);
            if (foundIdx !== -1 && allowed.indexOf(foundIdx) !== -1) {
                if (session.currentActiveIdx !== -1 && session.currentActiveIdx !== foundIdx) {
                    session.providerStates[session.currentActiveIdx] = { status: 'fail', pct: 100 };
                    session.targetPct = 5;
                }
                session.currentActiveIdx = foundIdx;
                session.targetPct = Math.max(session.targetPct, 35);
                if (!session.providerStates[foundIdx]) {
                    session.providerStates[foundIdx] = { status: 'active', pct: 5 };
                } else if (session.providerStates[foundIdx].pct < 5) {
                    session.providerStates[foundIdx].pct = 5;
                }
                session.providerStates[foundIdx].status = 'active';
            }

            for (var i = 0; i < SLS_PHASE_MAP.length; i++) {
                if (SLS_PHASE_MAP[i].match.test(text)) {
                    if (SLS_PHASE_MAP[i].pct > session.targetPct) {
                        session.targetPct = SLS_PHASE_MAP[i].pct;
                    }
                    break;
                }
            }
            saveSessionToStorage(appid);
            syncModalUI(appid);
        }, 150);
    }

    function finishGlobalTracking(appid, success) {
        var session = globalDownloadSessions[appid];
        if (!session || session.finished) return;
        session.finished = true;
        if (session.crawlId) clearInterval(session.crawlId);
        if (session.pollId) clearInterval(session.pollId);

        var allowed = session.allowedIndices || [];
        if (success) {
            if (session.currentActiveIdx !== -1) {
                session.providerStates[session.currentActiveIdx] = {
                    status: 'done',
                    pct: 100
                };
            }
        } else {
            allowed.forEach(function(pIdx) {
                session.providerStates[pIdx] = {
                    status: 'fail',
                    pct: 100
                };
            });
            if (session.currentActiveIdx !== -1) {
                session.providerStates[session.currentActiveIdx] = {
                    status: 'fail',
                    pct: 100
                };
            }
        }
        saveSessionToStorage(appid);
        syncModalUI(appid);
    }

    function openLuaProviderConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.dataset.slsAppid = String(appid);
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
        var order = getProviderOrder();
        var session = getOrCreateGlobalSession(appid);
        var requestedProviders = session.requestedProviders;

        var rows = '';
        order.forEach(function(providerIndex, position) {
            var provider = luaProviders[providerIndex];
            var apiKey = localStorage.getItem(provider.key) || '';
            var getKeyBtn = (provider.url && provider.url !== '#') ?
                '<button data-sls-getkey="' + providerIndex + '" title="Get API Key" style="color:#a5b4fc;font-size:12px;margin-left:4px;padding:2px 6px;border:1px solid #3b4252;border-radius:4px;background:#252a36;cursor:pointer;">Get Key</button>' : '';
                
            rows += '<div data-sls-provider="' + providerIndex + '" style="position:relative;overflow:hidden;display:flex;align-items:center;gap:8px;padding:10px 12px;margin-bottom:6px;background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);border-radius:8px;">' +
                '<strong style="padding-left:10px;position:relative;z-index:1;flex:1;color:#f5f6f8;font-size:13px;">' + provider.name + '</strong>' +
                '<div style="position:relative;z-index:1;display:flex;align-items:center;gap:6px;min-width:230px;border:1px solid #3b4252;border-radius:5px;padding:3px 6px;">' +
                '<span title="API key" aria-hidden="true" style="color:#a5b4fc;font-size:15px;">&#128273;</span>' +
                '<input data-sls-api-key="' + providerIndex + '" aria-label="API key for ' + escapeHtml(provider.name) + '" value="' + escapeHtml(apiKey) + '" placeholder="API key" type="text" style="min-width:0;flex:1;background:transparent;border:0;outline:0;color:#f5f6f8;padding:5px 2px;font-size:12px;" />' +
                getKeyBtn +
                '</div>' +
                '<button data-sls-up="' + providerIndex + '" title="Move provider up" style="position:relative;z-index:1;margin-right:4px;background:#252a36;border:0;color:#d6d7d9;padding:5px 8px;border-radius:5px;cursor:pointer;"' + (position === 0 ? ' disabled' : '') + '>Up</button>' +
                '<button data-sls-down="' + providerIndex + '" title="Move provider down" style="position:relative;z-index:1;margin-right:4px;background:#252a36;border:0;color:#d6d7d9;padding:5px 8px;border-radius:5px;cursor:pointer;"' + (position === order.length - 1 ? ' disabled' : '') + '>Down</button>' +
                '<button data-sls-provider-download="' + providerIndex + '" style="position:relative;z-index:1;margin-right:4px;background:#1a9fff;border:0;color:#fff;padding:5px 9px;border-radius:5px;cursor:pointer;">Download</button>' +
                '</div>';
        });
        overlay.innerHTML = '<div style="background:#161920;border:1px solid rgba(255,255,255,0.1);border-radius:12px;padding:24px;width:auto;max-width:calc(100% - 32px);box-shadow:0 20px 50px rgba(0,0,0,0.6);font-family:Arial,sans-serif;color:#f5f6f8;">' +
            '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:18px;"><div><h2 style="margin:0;font-size:20px;">Lua download provider</h2><p style="margin:5px 0 0;color:#8f98a0;font-size:12px;">AppID: ' + appid + '</p></div><button id="sls-provider-close" style="margin-right:4px;background:transparent;border:0;color:#9ca3af;font-size:20px;cursor:pointer;">X</button></div>' +
            '<div id="sls-provider-rows">' + rows + '</div>' +
            '<div style="display:flex;justify-content:flex-end;gap:8px;margin-top:18px;"><button id="sls-auto-search" style="min-width:100%;background:#4f46e5;border:0;color:#fff;padding:9px 14px;border-radius:6px;cursor:pointer;font-weight:600;">Auto Download</button></div>' +
            '</div>';
        document.body.appendChild(overlay);
        syncModalUI(appid);

        function close() { overlay.remove(); }
        document.getElementById('sls-provider-close').onclick = close;
        var cancelButton = document.getElementById('sls-provider-cancel');
        if (cancelButton) cancelButton.onclick = close;

        function disableProviderBtn(pIdx) {
            var btn = overlay.querySelector('[data-sls-provider-download="' + pIdx + '"]');
            if (btn) {
                // btn.disabled = true;
                // btn.style.opacity = '0.4';
                // btn.style.cursor = 'not-allowed';
            }
        }

        // Auto Download — try only unrequested providers sequentially
        document.getElementById('sls-auto-search').onclick = function() {
            var fullOrder = getProviderOrder();
            var remainingOrder = fullOrder.filter(function(pIdx) {
                return !requestedProviders[pIdx];
            });

            if (remainingOrder.length === 0) return;

            startGlobalDownloadTracking(appid, remainingOrder);
            sendLuaRequest(appid, null, remainingOrder);
        };

        overlay.querySelectorAll('[data-sls-api-key]').forEach(function(input) {
            function saveApiKey() {
                var provider = luaProviders[parseInt(input.getAttribute('data-sls-api-key'), 10)];
                localStorage.setItem(provider.key, input.value);
                sendApiKeys();
            }
            input.addEventListener('blur', saveApiKey);
            input.addEventListener('keydown', function(event) {
                if (event.key === 'Enter') {
                    event.preventDefault();
                    saveApiKey();
                    input.blur();
                }
            });
        });
        overlay.querySelectorAll('[data-sls-up], [data-sls-down]').forEach(function(button) {
            button.onclick = function() {
                var isUp = button.hasAttribute('data-sls-up');
                var rowEl = button.closest ? button.closest('[data-sls-provider]') : button.parentElement;
                if (!rowEl) return;
                var container = rowEl.parentElement;
                if (!container) return;

                if (isUp) {
                    var prev = rowEl.previousElementSibling;
                    if (prev) container.insertBefore(rowEl, prev);
                } else {
                    var next = rowEl.nextElementSibling;
                    if (next) container.insertBefore(next, rowEl);
                }

                var allRows = Array.from(container.children);
                var newOrder = allRows.map(function(child) {
                    return parseInt(child.getAttribute('data-sls-provider'), 10);
                });
                saveProviderOrder(newOrder);

                allRows.forEach(function(child, idx) {
                    var upBtn = child.querySelector('[data-sls-up]');
                    var downBtn = child.querySelector('[data-sls-down]');
                    if (upBtn) upBtn.disabled = (idx === 0);
                    if (downBtn) downBtn.disabled = (idx === allRows.length - 1);
                });
            };
        });
        // Single provider download — block only the requested provider, modal stays open
        overlay.querySelectorAll('[data-sls-provider-download]').forEach(function(button) {
            button.onclick = function() {
                var pIdx = parseInt(button.getAttribute('data-sls-provider-download'), 10);
                // if (requestedProviders[pIdx]) return;

                startGlobalDownloadTracking(appid, [pIdx]);
                sendLuaRequest(appid, pIdx);
            };
        });
        // Open provider page in a new Steam browser tab to get an API key
        overlay.querySelectorAll('[data-sls-getkey]').forEach(function(button) {
            button.onclick = function(e) {
                e.preventDefault();
                e.stopPropagation();
                var pIdx = parseInt(button.getAttribute('data-sls-getkey'), 10);
                var provider = luaProviders[pIdx];
                if (provider && provider.url && provider.url !== '#') {
                    window.location.href = provider.url;
                }
            };
        });
    }

    function setupDownloadButton(luaLink, luaBtn, productID, cartBtn) {
        var span = luaLink.querySelector('span');
        if (span) span.innerText = 'Download Lua';
        luaLink.style.filter = 'hue-rotate(110deg) brightness(1.2)';
        luaLink.style.pointerEvents = '';
        luaLink.style.opacity = '';
        luaLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            luaBtn.dataset.slsAppid = productID;
            ping('Lua Click: ' + productID);
            openLuaProviderConfig(productID);
        };
    }

    /**
     * Called by the C++ side via CDP injection when a download completes or
     * fails while the store page is open (i.e. pushStatus calls come through
     * as innerText changes on the button span).  The loading bar already polls
     * those text changes via startRowLoadingBar — no extra wiring needed here.
     * This hook exists so any future direct-JS callers can still trigger bars.
     */
    function slsTriggerLoadingBar(productID) {
        var btn  = document.querySelector('.sls-lua-btn[data-sls-appid="' + productID + '"]');
        var link = btn ? btn.querySelector('a') : null;
        if (btn && link) startRowLoadingBar(btn, link);
    }
    window.slsTriggerLoadingBar = slsTriggerLoadingBar;

    function setupRemoveButton(luaLink, luaBtn, productID, cartBtn) {
        var span = luaLink.querySelector('span');
        if (span) span.innerText = 'Already installed';
        luaLink.style.filter = 'hue-rotate(320deg) brightness(1.1)';
        luaLink.style.pointerEvents = '';
        luaLink.style.opacity = '';
        luaLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();

            // Show confirmation modal
            if (document.getElementById('sls-remove-overlay')) return;
            var overlay = document.createElement('div');
            overlay.id = 'sls-remove-overlay';
            overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(5px);';
            overlay.innerHTML = '<div style="background:#1a1c23;border:1px solid #2a2d36;border-radius:12px;padding:30px;width:400px;box-shadow:0 15px 30px rgba(0,0,0,0.5);font-family:Inter,sans-serif;color:#fff;text-align:center;">' +
                '<h2 style="margin:0 0 10px;font-size:20px;font-weight:600;color:#e8e9eb;">Remove Lua</h2>' +
                '<p style="margin:0 0 20px;font-size:13px;color:#8a8d96;">Remove Lua and Game files for AppID <b>' + productID + '</b>?</p>' +
                '<div style="display:flex;justify-content:center;gap:10px;">' +
                    '<button id="sls-rm-cancel" style="background:transparent;border:1px solid #333640;color:#e8e9eb;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;">Cancel</button>' +
                    '<button id="sls-rm-confirm" style="background:#ff4d4d;border:none;color:#fff;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;box-shadow:0 4px 10px rgba(255,77,77,0.3);">Remove</button>' +
                '</div>' +
            '</div>';
            document.body.appendChild(overlay);

            document.getElementById('sls-rm-cancel').onclick = function() { overlay.remove(); };
            document.getElementById('sls-rm-confirm').onclick = function() {
                var confirmBtn = document.getElementById('sls-rm-confirm');
                confirmBtn.innerText = 'Processing...';
                confirmBtn.style.opacity = '0.5';
                // confirmBtn.style.pointerEvents = 'none';

                ping('Remove Lua: ' + productID);
                window.location.hash = 'sls-click-removelua-' + productID + '-' + Date.now();

                // Update cached status
                appUnlockStatus[productID] = { exists: false, pending: false, onlineFixInstalled: false, autoCrackInstalled: false };

                overlay.remove();

                // Switch button back to Download Lua
                setupDownloadButton(luaLink, luaBtn, productID, cartBtn);

            };
        };
    }

    function openSlsConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
        var currentMorr = localStorage.getItem('sls-morr-key') || '%MORR_KEY%';
        var currentRyuu = localStorage.getItem('sls-ryuu-key') || '%RYUU_KEY%';
        var currentDpbx = localStorage.getItem('sls-dpbx-key') || '%DPBX_KEY%';
        var currentHubcap = localStorage.getItem('sls-hubcap-key') || '%HUBCAP_KEY%';

        var cardHtml = '<div style="background: linear-gradient(145deg, #161920 0%, #0d0f14 100%); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 16px; padding: 28px; width: 440px; box-shadow: 0 20px 50px rgba(0,0,0,0.6); font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, Helvetica, Arial, sans-serif; color: #f5f6f8; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;" id="sls-modal-card">' +
            '<!-- Title bar -->' +
            '<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:24px;">' +
                '<div>' +
                    '<h2 style="margin:0; font-size:22px; font-weight:700; background: linear-gradient(90deg, #fff 0%, #a5aab6 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent;">SLS Game Manager</h2>' +
                    '<p style="margin:4px 0 0; font-size:12px; color:#6b7280; font-weight: 500;">AppID: <span style="color:#9ca3af; font-family:monospace;">' + appid + '</span></p>' +
                '</div>' +
                '<button id="sls-close-x" style="background:rgba(255,255,255,0.05); border:none; color:#9ca3af; font-size:20px; cursor:pointer; width:32px; height:32px; border-radius:50%; display:flex; align-items:center; justify-content:center; transition: all 0.2s;">&times;</button>' +
            '</div>' +
            '<!-- API Settings Section -->' +
            '<div>' +
                '<h3 style="margin:0 0 14px; font-size:12px; font-weight:700; text-transform:uppercase; letter-spacing:1px; color:#4f46e5;">API Credentials</h3>' +
                '<div style="margin-bottom:16px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#9ca3af;">' +
                        '<span>HubcapDB API Key</span>' +
                        '<a href="https://hubcapmanifest.com/" target="_blank" style="color:#6366f1; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-hubcap" type="text" value="' + currentHubcap + '" style="width:100%; box-sizing:border-box; background:#090a0f; border:1px solid rgba(255,255,255,0.08); color:#f5f6f8; padding:10px 14px; border-radius:8px; font-family:monospace; font-size:13px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
                '<div style="margin-bottom:16px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#9ca3af;">' +
                        '<span>Morrenus API Key</span>' +
                        '<a href="https://manifest.morrenus.xyz/api-keys/stats" target="_blank" style="color:#6366f1; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-morr" type="text" value="' + currentMorr + '" style="width:100%; box-sizing:border-box; background:#090a0f; border:1px solid rgba(255,255,255,0.08); color:#f5f6f8; padding:10px 14px; border-radius:8px; font-family:monospace; font-size:13px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
                '<div style="margin-bottom:24px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#9ca3af;">' +
                        '<span>Ryuu API Key</span>' +
                        '<a href="https://generator.ryuu.lol/" target="_blank" style="color:#6366f1; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-ryuu" type="text" value="' + currentRyuu + '" style="width:100%; box-sizing:border-box; background:#090a0f; border:1px solid rgba(255,255,255,0.08); color:#f5f6f8; padding:10px 14px; border-radius:8px; font-family:monospace; font-size:13px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
                '<div style="margin-bottom:24px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#9ca3af;">' +
                        '<span>DepotBox API Key</span>' +
                        '<a href="https://depotbox.org/pricing" target="_blank" style="color:#6366f1; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-dpbx" type="text" value="' + currentDpbx + '" style="width:100%; box-sizing:border-box; background:#090a0f; border:1px solid rgba(255,255,255,0.08); color:#f5f6f8; padding:10px 14px; border-radius:8px; font-family:monospace; font-size:13px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
            '</div>' +
            '<!-- Footer Actions -->' +
            '<div style="display:flex; justify-content:flex-end; gap:12px;">' +
                '<button id="sls-btn-cancel" style="background:transparent; border:1px solid rgba(255,255,255,0.1); color:#9ca3af; padding:10px 20px; border-radius:8px; cursor:pointer; font-size:13px; font-weight:600; transition: all 0.2s;">Cancel</button>' +
                '<button id="sls-btn-save" style="background:linear-gradient(135deg, #4f46e5 0%, #3730a3 100%); border:none; color:#fff; padding:10px 20px; border-radius:8px; cursor:pointer; font-size:13px; font-weight:600; box-shadow:0 4px 12px rgba(79,70,229,0.3); transition: all 0.2s;">Save Keys</button>' +
            '</div>' +
        '</div>';

        overlay.innerHTML = cardHtml;
        var style = document.createElement('style');
        style.textContent = ' #sls-close-x:hover { background: rgba(255,255,255,0.1) !important; color: #fff !important; }' +
            ' #sls-btn-cancel:hover { background: rgba(255,255,255,0.03) !important; color: #fff !important; border-color: rgba(255,255,255,0.2) !important; }' +
            ' #sls-btn-save:hover { box-shadow: 0 6px 20px rgba(79,70,229,0.45) !important; }' +
            ' #sls-hubcap:focus, #sls-morr:focus, #sls-ryuu:focus, #sls-dpbx:focus { border-color: #6366f1 !important; box-shadow: 0 0 0 2px rgba(99,102,241,0.2) !important; }';

        overlay.appendChild(style);
        document.body.appendChild(overlay);

        requestAnimationFrame(function() {
            overlay.style.opacity = '1';
            var card = document.getElementById('sls-modal-card');
            if (card) {
                card.style.transform = 'scale(1)';
                card.style.opacity = '1';
            }
        });

        var closeX = document.getElementById('sls-close-x');
        var cancelBtn = document.getElementById('sls-btn-cancel');
        var saveBtn = document.getElementById('sls-btn-save');

        function close() {
            overlay.style.opacity = '0';
            var card = document.getElementById('sls-modal-card');
            if (card) {
                card.style.transform = 'scale(0.95)';
                card.style.opacity = '0';
            }
            setTimeout(function() { overlay.remove(); }, 300);
        }
        closeX.onclick = close;
        cancelBtn.onclick = close;

        saveBtn.onclick = function() {
            var hubcap = document.getElementById('sls-hubcap').value;
            var morr = document.getElementById('sls-morr').value;
            var ryuu = document.getElementById('sls-ryuu').value;
            var dpbx = document.getElementById('sls-dpbx').value;
            localStorage.setItem('sls-hubcap-key', hubcap);
            localStorage.setItem('sls-morr-key', morr);
            localStorage.setItem('sls-ryuu-key', ryuu);
            localStorage.setItem('sls-dpbx-key', dpbx);
            window.location.hash = 'sls-auth-MORR=' + encodeURIComponent(morr) + '&RYUU=' + encodeURIComponent(ryuu) + '&DPBX=' + encodeURIComponent(dpbx) + '&HUBCAP=' + encodeURIComponent(hubcap) + '-TS=' + Date.now();
            close();

            var toast = document.createElement('div');
            toast.innerText = 'API Settings Saved!';
            toast.style.cssText = 'position:fixed;bottom:30px;right:30px;background:#28a745;color:#fff;padding:12px 20px;border-radius:8px;font-family:Inter,sans-serif;font-weight:500;z-index:999999;box-shadow:0 5px 15px rgba(0,0,0,0.3);transition:opacity 0.5s;';
            document.body.appendChild(toast);
            setTimeout(function(){ toast.style.opacity = '0'; }, 2000);
            setTimeout(function(){ toast.remove(); }, 2500);
        };
    }

    function addButtons() {
        if (observer) observer.disconnect();

        var cartBtns = document.querySelectorAll('.btn_addtocart, .btn_add_to_cart');
        for (var i = 0; i < cartBtns.length; i++) {
            var cartBtn = cartBtns[i];
            if (cartBtn.dataset.slsProcessed) continue;
            if (cartBtn.classList.contains('sls-lua-btn')) continue;
            var link = cartBtn.querySelector('a');
            if (!link) continue;
            var hrefLower = link.href.toLowerCase();
            if (hrefLower.indexOf('bundle') !== -1 || hrefLower.indexOf('dlc') !== -1) continue;
            var productID = null;
            var match = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
            if (match) {
                productID = match[2];
            }
            if (productID) {
                cartBtn.dataset.slsProcessed = '1';
                var luaBtn = cartBtn.cloneNode(true);
                luaBtn.classList.remove('btn_addtocart');
                luaBtn.classList.remove('btn_add_to_cart');
                luaBtn.classList.add('sls-lua-btn');
                luaBtn.dataset.slsProcessed = '1';
                luaBtn.dataset.slsAppid = productID;
                luaBtn.style.display = 'inline-block';
                luaBtn.style.marginRight = '4px';
                luaBtn.style.float = 'right';
                var luaLink = luaBtn.querySelector('a');
                if (luaLink) {
                    luaLink.href = 'javascript:void(0)';
                    luaLink.removeAttribute('id');

                    // Avoid showing the download action until the installed state is known.
                    var initialSpan = luaLink.querySelector('span');
                    if (initialSpan) initialSpan.innerText = 'Checking Lua...';
                    luaLink.style.filter = 'hue-rotate(200deg) brightness(1.0)';
                    // luaLink.style.pointerEvents = 'none';
                    
                    // Allow clicking even while checking
                    luaLink.onclick = function(e) {
                        e.preventDefault();
                        e.stopPropagation();
                        luaBtn.dataset.slsAppid = productID;
                        ping('Lua Click (early): ' + productID);
                        openLuaProviderConfig(productID);
                    };

                    // Check unlock status via callback server
                    if (appUnlockStatus[productID] !== undefined) {
                        var cached = appUnlockStatus[productID];
                        if (cached && (cached.exists || cached.pending)) {
                            setupRemoveButton(luaLink, luaBtn, productID, cartBtn);
                        } else {
                            setupDownloadButton(luaLink, luaBtn, productID, cartBtn);
                        }
                    } else {
                        // Query the server with a short timeout
                        (function(ll, lb, pid, cb) {
                            var controller = new AbortController();
                            var timeoutId = setTimeout(function() { controller.abort(); }, 1000);
                            fetch('http://127.0.0.1:9001/check?id=' + pid, { signal: controller.signal })
                                .then(function(r) { clearTimeout(timeoutId); return r.json(); })
                                .then(function(data) {
                                    appUnlockStatus[pid] = data;
                                    var isUnlocked = data.exists || data.pending;
                                    if (isUnlocked) {
                                        setupRemoveButton(ll, lb, pid, cb);
                                    } else {
                                        setupDownloadButton(ll, lb, pid, cb);
                                    }
                                })
                                .catch(function() {
                                    clearTimeout(timeoutId);
                                    ping('Check failed for ' + pid + ', defaulting to Download');
                                    setupDownloadButton(ll, lb, pid, cb);
                                });
                        })(luaLink, luaBtn, productID, cartBtn);
                    }
                }
                var header = document.querySelector('div.apphub_HeaderStandardTop') ||
                    document.querySelector('.apphub_HeaderStandardTop');
                if (header) {
                    var appName = null;
                    for (var childIndex = 0; childIndex < header.children.length; childIndex++) {
                        if (header.children[childIndex].classList.contains('apphub_AppName')) {
                            appName = header.children[childIndex];
                            break;
                        }
                    }
                    var insertionPoint = appName || header.firstChild;
                    header.insertBefore(luaBtn, insertionPoint);
                } else {
                    cartBtn.parentNode.insertBefore(luaBtn, cartBtn.nextSibling);
                }
                break;
            }
        }

        // Auto-resume download tracking if there is an active session for this product
        var pageMatch = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
        if (pageMatch && pageMatch[2]) {
            var currentAppid = pageMatch[2];
            var saved = loadSessionFromStorage(currentAppid);
            if (saved && !saved.finished) {
                startGlobalDownloadTracking(currentAppid, null);
            }
        }

        if (observer && document.body) observer.observe(document.body, { childList: true, subtree: true });
    }

    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function() {
            debounceTimer = null;
            addButtons();
        });
    }

    addButtons();
    observer = new MutationObserver(debouncedAddButtons);
    if (document.body) observer.observe(document.body, { childList: true, subtree: true });
})();
