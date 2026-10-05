(function() {
    if (window.__slsLuaBtnAdded) return;
    window.__slsLuaBtnAdded = true;
    console.log('[SLS] Store Page Script Triggered');

    function ping(msg) { console.log('[SLS] StorePage: ' + msg); }

    ping('Script active on: ' + window.location.href);

    var observer = null;
    var debounceTimer = null;

    function trapSteamGamepadModal(overlay, buttons) {
        var isGrid = Array.isArray(buttons[0]);
        var grid = isGrid ? buttons : buttons.map(function(b) { return [b]; });
        var activeR = 0;
        var activeC = 0;

        function updateFocus() {
            for (var r = 0; r < grid.length; r++) {
                for (var c = 0; c < grid[r].length; c++) {
                    var btn = grid[r][c];
                    if (!btn) continue;
                    if (r === activeR && c === activeC) {
                        btn.style.outline = '3px solid #fff';
                        btn.style.outlineOffset = '2px';
                        if (btn.scrollIntoViewIfNeeded) btn.scrollIntoViewIfNeeded();
                        else btn.scrollIntoView({ block: 'nearest', inline: 'nearest' });
                    } else {
                        btn.style.outline = 'none';
                    }
                }
            }
        }
        setTimeout(updateFocus, 100);
        
        function gamepadCatcher(e) {
            if (!document.body.contains(overlay)) {
                window.removeEventListener('vgp_ondirection', gamepadCatcher, true);
                window.removeEventListener('vgp_onok', okCatcher, true);
                window.removeEventListener('vgp_oncancel', cancelCatcher, true);
                return;
            }
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var dir = e.detail ? e.detail.button : null;
            var startR = activeR, startC = activeC;
            
            function move(dr, dc) {
                var newR = activeR, newC = activeC;
                var attempts = 0;
                var maxAttempts = grid.length * 10;
                
                // Calculate alignment from the RIGHT side to handle ragged rows
                var targetOffsetRight = grid[activeR].length - 1 - activeC;
                
                while (attempts < maxAttempts) {
                    attempts++;
                    if (dr !== 0) {
                        newR += dr;
                        if (newR < 0) newR = grid.length - 1;
                        if (newR >= grid.length) newR = 0;
                        
                        // Apply right-alignment to the new row
                        newC = grid[newR].length - 1 - targetOffsetRight;
                        if (newC < 0) newC = 0; // Clamp to left if new row is too short
                        if (newC >= grid[newR].length) newC = grid[newR].length - 1;
                    }
                    if (dc !== 0) {
                        newC += dc;
                        if (newC < 0) newC = grid[newR].length - 1;
                        if (newC >= grid[newR].length) newC = 0;
                    }
                    var targetBtn = grid[newR][newC];
                    if (targetBtn) {
                        activeR = newR;
                        activeC = newC;
                        return;
                    }
                }
            }

            if (dir === 9 || dir === 14) move(-1, 0); // Up
            else if (dir === 10 || dir === 15) move(1, 0); // Down
            else if (dir === 11 || dir === 16) move(0, -1); // Left
            else if (dir === 12 || dir === 17) move(0, 1); // Right
            
            updateFocus();
        }
        function okCatcher(e) {
            if (!document.body.contains(overlay)) return;
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var btn = grid[activeR] ? grid[activeR][activeC] : null;
            if (btn && !btn.disabled) {
                if (btn.tagName.toLowerCase() === 'input') {
                    btn.focus();
                    btn.select();
                }
                btn.click();
            }
        }
        function cancelCatcher(e) {
            if (!document.body.contains(overlay)) return;
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var closeBtn = overlay.querySelector('#sls-provider-close, #sls-close-x, #sls-btn-cancel, #sls-prov-cancel, #sls-rm-cancel, #sls-depot-close-x, #sls-depot-btn-cancel');
            if (closeBtn) closeBtn.click(); else overlay.remove();
        }
        window.addEventListener('vgp_ondirection', gamepadCatcher, true);
        window.addEventListener('vgp_onok', okCatcher, true);
        window.addEventListener('vgp_oncancel', cancelCatcher, true);
    }

    function registerSteamFocusNode(element, properties) {
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
                '<button data-sls-getkey="' + providerIndex + '" class="Focusable" tabindex="-1" role="button" title="Get API Key" style="color:#dcdedf;font-size:12px;margin-left:4px;padding:2px 8px;border:1px solid #3d4450;border-radius:2px;background:rgba(172,178,201,0.14);cursor:pointer;">Get Key</button>' : '';
                
            rows += '<div data-sls-provider="' + providerIndex + '" style="position:relative;overflow:hidden;display:flex;align-items:center;gap:8px;padding:12px 14px;margin-bottom:6px;background:rgba(0,0,0,0.2);border:1px solid rgba(172,178,201,0.1);border-radius:2px;">' +
                '<strong style="padding-left:10px;position:relative;z-index:1;flex:1;color:#dcdedf;font-size:16px;font-family:\'Motiva Sans\', sans-serif;font-weight:400;">' + provider.name + '</strong>' +
                '<div style="position:relative;z-index:1;display:flex;align-items:center;gap:6px;min-width:230px;border:1px solid #3d4450;border-radius:2px;padding:4px 8px;background:rgba(0,0,0,0.4);">' +
                '<span title="API key" aria-hidden="true" style="color:#969696;font-size:15px;">&#128273;</span>' +
                '<input data-sls-api-key="' + providerIndex + '" aria-label="API key for ' + escapeHtml(provider.name) + '" value="' + escapeHtml(apiKey) + '" placeholder="API key" type="text" style="min-width:0;flex:1;background:transparent;border:0;outline:0;color:#dcdedf;padding:5px 2px;font-size:14px;font-family:\'Motiva Sans\', sans-serif;" />' +
                getKeyBtn +
                '</div>' +
                '<div style="display:flex;gap:4px;position:relative;z-index:1;">' +
                '<button data-sls-up="' + providerIndex + '" class="sls-btn-action Focusable" tabindex="-1" role="button" title="Move provider up" style="height:32px;padding:0 8px;font-size:12px;"' + (position === 0 ? ' disabled' : '') + '>Up</button>' +
                '<button data-sls-down="' + providerIndex + '" class="sls-btn-action Focusable" tabindex="-1" role="button" title="Move provider down" style="height:32px;padding:0 8px;font-size:12px;"' + (position === order.length - 1 ? ' disabled' : '') + '>Down</button>' +
                '</div>' +
                '<button data-sls-provider-download="' + providerIndex + '" class="sls-btn-action Focusable" tabindex="-1" role="button" style="position:relative;z-index:1;height:32px;padding:0 16px;background:#06bfff;color:#fff;">Download</button>' +
                '</div>';
        });
        overlay.innerHTML = '<div style="background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 680px; max-width:calc(100% - 32px); box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: \'Motiva Sans\', sans-serif; color: #dcdedf;">' +
            '<div style="display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:20px;">' +
            '<div><h2 style="margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;">Lua download provider</h2><p style="margin:4px 0 0;font-size:14px;color:#969696;">AppID: ' + appid + '</p></div>' +
            '<button id="sls-provider-close" class="sls-btn-close Focusable" tabindex="-1" role="button">&times;</button></div>' +
            '<div id="sls-provider-rows">' + rows + '</div>' +
            '<div style="display:flex;justify-content:flex-end;gap:10px;margin-top:20px;"><button id="sls-auto-search" class="sls-btn-action Focusable" tabindex="-1" role="button" style="min-width:100%;">Auto Download</button></div>' +
            '<style>' +
            ' .sls-btn-action { display: flex; justify-content: center; align-items: center; background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); border: none; border-radius: 2px; padding: 8px 16px; font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; height: 48px; box-sizing: border-box; cursor: pointer; text-decoration: none; transition: background 0.1s ease; }' +
            ' .sls-btn-action:not(:disabled):hover { background: rgba(172, 178, 201, 0.25); }' +
            ' .sls-btn-action:not(:disabled):active { background: rgba(172, 178, 201, 0.30); }' +
            ' button[data-sls-provider-download]:hover { background: #2d73ff !important; }' +
            ' .sls-btn-close { background: transparent; border: none; color: #969696; font-size: 24px; cursor: pointer; width: 32px; height: 32px; border-radius: 2px; display: flex; align-items: center; justify-content: center; transition: background 0.1s ease; }' +
            ' .sls-btn-close:hover { background: rgba(172, 178, 201, 0.14); color: #fff; }' +
            ' button:disabled { opacity: 0.5; cursor: default; }' +
            '</style>' +
            '</div>';
        document.body.appendChild(overlay);
        syncModalUI(appid);

        // Controller support: collect interactive elements into a 2D grid
        var modalButtons = [
            [document.getElementById('sls-provider-close')]
        ];
        overlay.querySelectorAll('[data-sls-provider]').forEach(function(row) {
            var rowItems = [];
            row.querySelectorAll('[data-sls-api-key], [data-sls-getkey], [data-sls-up], [data-sls-down], [data-sls-provider-download]').forEach(function(b) { rowItems.push(b); });
            if (rowItems.length > 0) modalButtons.push(rowItems);
        });
        modalButtons.push([document.getElementById('sls-auto-search')]);
        trapSteamGamepadModal(overlay, modalButtons);

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
            overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
            overlay.innerHTML = '<div style="background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: \'Motiva Sans\', sans-serif; color: #dcdedf;">' +
                '<h2 style="margin:0 0 10px;font-size:22px;font-weight:300;color:#fff;text-transform:uppercase;letter-spacing:1px;">Remove Lua</h2>' +
                '<p style="margin:0 0 24px;font-size:16px;color:#969696;">Remove Lua and Game files for AppID <b>' + productID + '</b>?</p>' +
                '<div style="display:flex;justify-content:flex-end;gap:10px;">' +
                    '<button id="sls-rm-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px;">Cancel</button>' +
                    '<button id="sls-rm-confirm" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px; background: rgba(220, 38, 38, 0.8); color: #fff;">Remove</button>' +
                '</div>' +
                '<style>' +
                ' .sls-btn-action { display: flex; justify-content: center; align-items: center; background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); border: none; border-radius: 2px; padding: 8px 16px; font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; height: 48px; box-sizing: border-box; cursor: pointer; text-decoration: none; transition: background 0.1s ease; }' +
                ' .sls-btn-action:hover { background: rgba(172, 178, 201, 0.25); }' +
                ' .sls-btn-action:active { background: rgba(172, 178, 201, 0.30); }' +
                ' #sls-rm-confirm:hover { background: rgba(220, 38, 38, 1) !important; }' +
                '</style>' +
            '</div>';
            document.body.appendChild(overlay);

            var rmCancelBtn = document.getElementById('sls-rm-cancel');
            var rmConfirmBtn = document.getElementById('sls-rm-confirm');
            trapSteamGamepadModal(overlay, [rmCancelBtn, rmConfirmBtn]);

            rmCancelBtn.onclick = function() { overlay.remove(); };
            rmConfirmBtn.onclick = function() {
                rmConfirmBtn.innerText = 'Processing...';
                rmConfirmBtn.style.opacity = '0.5';
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
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';
        var currentMorr = localStorage.getItem('sls-morr-key') || '%MORR_KEY%';
        var currentRyuu = localStorage.getItem('sls-ryuu-key') || '%RYUU_KEY%';
        var currentDpbx = localStorage.getItem('sls-dpbx-key') || '%DPBX_KEY%';
        var currentHubcap = localStorage.getItem('sls-hubcap-key') || '%HUBCAP_KEY%';

        var cardHtml = '<div style="background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: \'Motiva Sans\', sans-serif; color: #dcdedf; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;" id="sls-modal-card">' +
            '<!-- Title bar -->' +
            '<div style="display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:20px;">' +
                '<div>' +
                    '<h2 style="margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;">API Credentials</h2>' +
                    '<p style="margin:4px 0 0; font-size:14px; color:#969696;">AppID: ' + appid + '</p>' +
                '</div>' +
                '<button id="sls-close-x" class="sls-btn-close Focusable" tabindex="-1" role="button">&times;</button>' +
            '</div>' +
            '<!-- API Settings Section -->' +
            '<div>' +
                '<div style="margin-bottom:16px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#969696;">' +
                        '<span>HubcapDB API Key</span>' +
                        '<a href="https://hubcapmanifest.com/" class="Focusable" tabindex="-1" role="button" target="_blank" style="color:#06bfff; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-hubcap" type="text" value="' + currentHubcap + '" style="width:100%; box-sizing:border-box; background:rgba(0,0,0,0.4); border:1px solid #3d4450; color:#dcdedf; padding:10px 14px; border-radius:2px; font-family:\'Motiva Sans\', sans-serif; font-size:14px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
                '<div style="margin-bottom:16px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#969696;">' +
                        '<span>Morrenus API Key</span>' +
                        '<a href="https://manifest.morrenus.xyz/api-keys/stats" class="Focusable" tabindex="-1" role="button" target="_blank" style="color:#06bfff; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-morr" type="text" value="' + currentMorr + '" style="width:100%; box-sizing:border-box; background:rgba(0,0,0,0.4); border:1px solid #3d4450; color:#dcdedf; padding:10px 14px; border-radius:2px; font-family:\'Motiva Sans\', sans-serif; font-size:14px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
                '<div style="margin-bottom:16px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#969696;">' +
                        '<span>Ryuu API Key</span>' +
                        '<a href="https://generator.ryuu.lol/" class="Focusable" tabindex="-1" role="button" target="_blank" style="color:#06bfff; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-ryuu" type="text" value="' + currentRyuu + '" style="width:100%; box-sizing:border-box; background:rgba(0,0,0,0.4); border:1px solid #3d4450; color:#dcdedf; padding:10px 14px; border-radius:2px; font-family:\'Motiva Sans\', sans-serif; font-size:14px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
                '<div style="margin-bottom:24px;">' +
                    '<div style="display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#969696;">' +
                        '<span>DepotBox API Key</span>' +
                        '<a href="https://depotbox.org/pricing" class="Focusable" tabindex="-1" role="button" target="_blank" style="color:#06bfff; text-decoration:none; transition: color 0.2s;">Get Key</a>' +
                    '</div>' +
                    '<input id="sls-dpbx" type="text" value="' + currentDpbx + '" style="width:100%; box-sizing:border-box; background:rgba(0,0,0,0.4); border:1px solid #3d4450; color:#dcdedf; padding:10px 14px; border-radius:2px; font-family:\'Motiva Sans\', sans-serif; font-size:14px; outline:none; transition: all 0.2s;" placeholder="Optional..."/>' +
                '</div>' +
            '</div>' +
            '<!-- Footer Actions -->' +
            '<div style="display:flex; justify-content:flex-end; gap:10px;">' +
                '<button id="sls-btn-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px;">Cancel</button>' +
                '<button id="sls-btn-save" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px; background: #06bfff; color: #fff;">Save Keys</button>' +
            '</div>' +
        '</div>';

        overlay.innerHTML = cardHtml;
        var style = document.createElement('style');
        style.textContent = ' .sls-btn-action { display: flex; justify-content: center; align-items: center; background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); border: none; border-radius: 2px; padding: 8px 16px; font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; height: 48px; box-sizing: border-box; cursor: pointer; text-decoration: none; transition: background 0.1s ease; }' +
            ' .sls-btn-action:not(:disabled):hover { background: rgba(172, 178, 201, 0.25); }' +
            ' .sls-btn-action:not(:disabled):active { background: rgba(172, 178, 201, 0.30); }' +
            ' #sls-btn-save:hover { background: #2d73ff !important; }' +
            ' .sls-btn-close { background: transparent; border: none; color: #969696; font-size: 24px; cursor: pointer; width: 32px; height: 32px; border-radius: 2px; display: flex; align-items: center; justify-content: center; transition: background 0.1s ease; }' +
            ' .sls-btn-close:hover { background: rgba(172, 178, 201, 0.14); color: #fff; }' +
            ' #sls-hubcap:focus, #sls-morr:focus, #sls-ryuu:focus, #sls-dpbx:focus { border-color: #6366f1 !important; }';

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

        trapSteamGamepadModal(overlay, [closeX, saveBtn, cancelBtn]);

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

        // Big Picture Mode detection
        var bpSummary = document.getElementById('summaryBarTop');
        var bpWrapper = document.getElementById('carouselContainerWrapper');
        var match = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
        
        if (bpSummary && bpWrapper && match) {
            var productID = match[2];
            if (!document.getElementById('sls-bp-lua-btn-container-' + productID)) {
                var bpContainer = document.createElement('div');
                bpContainer.id = 'sls-bp-lua-btn-container-' + productID;
                bpContainer.style.cssText = 'display: flex; justify-content: center; z-index: 100; position: relative; margin: 12px 0px 12px 0px; width: 100%; box-sizing: border-box;';

                var bpBtn = document.createElement('button');
                bpBtn.id = 'sls-bp-lua-btn-' + productID;
                bpBtn.className = 'Focusable sls-lua-btn';
                bpBtn.dataset.slsProcessed = '1';
                bpBtn.dataset.slsAppid = productID;
                bpBtn.style.cssText = 'background: linear-gradient(135deg, #1a9fff 0%, #0073d4 100%); border: none; color: white; padding: 12px 30px; font-size: 18px; font-weight: bold; border-radius: 6px; cursor: pointer; box-shadow: 0 4px 15px rgba(0,0,0,0.4); text-transform: uppercase; transition: all 0.2s; width: 100%; display: flex; justify-content: center; box-sizing: border-box;';
                
                var bpLink = document.createElement('a');
                bpLink.style.display = 'block';
                bpLink.style.color = 'white';
                bpLink.style.textDecoration = 'none';

                var bpSpan = document.createElement('span');
                bpSpan.innerText = 'Checking Lua...';
                
                bpLink.appendChild(bpSpan);
                bpBtn.appendChild(bpLink);
                bpContainer.appendChild(bpBtn);
                
                bpSummary.parentNode.insertBefore(bpContainer, bpSummary);
                
                bpLink.onclick = function(e) {
                    e.preventDefault();
                    e.stopPropagation();
                    ping('Lua Click (early): ' + productID);
                    openLuaProviderConfig(productID);
                };

                // Check unlock status via callback server
                if (appUnlockStatus[productID] !== undefined) {
                    var cached = appUnlockStatus[productID];
                    if (cached && (cached.exists || cached.pending)) {
                        setupRemoveButton(bpLink, bpBtn, productID, null);
                    } else {
                        setupDownloadButton(bpLink, bpBtn, productID, null);
                    }
                } else {
                    (function(ll, lb, pid) {
                        var controller = new AbortController();
                        var timeoutId = setTimeout(function() { controller.abort(); }, 1000);
                        fetch('http://127.0.0.1:9001/check?id=' + pid, { signal: controller.signal })
                            .then(function(r) { clearTimeout(timeoutId); return r.json(); })
                            .then(function(data) {
                                appUnlockStatus[pid] = data;
                                var isUnlocked = data.exists || data.pending;
                                if (isUnlocked) {
                                    setupRemoveButton(ll, lb, pid, null);
                                } else {
                                    setupDownloadButton(ll, lb, pid, null);
                                }
                            })
                            .catch(function() {
                                clearTimeout(timeoutId);
                                ping('Check failed for ' + pid + ', defaulting to Download');
                                setupDownloadButton(ll, lb, pid, null);
                            });
                    })(bpLink, bpBtn, productID);
                }

                // Register for gamepad navigation
                setTimeout(function() {
                    registerSteamFocusNode(bpBtn);
                    bpBtn.addEventListener('vgp_onfocus', function() {
                        bpBtn.style.outline = '4px solid #fff';
                        bpBtn.style.outlineOffset = '2px';
                        bpBtn.style.transform = 'scale(1.05)';
                    });
                    bpBtn.addEventListener('vgp_onblur', function() {
                        bpBtn.style.outline = 'none';
                        bpBtn.style.transform = 'scale(1)';
                    });
                    bpBtn.addEventListener('vgp_onok', function(e) {
                        e.preventDefault(); e.stopPropagation();
                        bpLink.click();
                    });
                }, 100);
            }
        }

        var isBigPicture = document.getElementById('summaryBarTop') || document.getElementById('carouselContainerWrapper') || document.querySelector('.gamepadui') || (document.body && document.body.classList.contains('gamepadui'));
        if (isBigPicture) {
            var rogueDesktopBtn = document.querySelector('.btn_addtocart.sls-lua-btn');
            if (rogueDesktopBtn) rogueDesktopBtn.remove();
        }
        var desktopMatch = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
        if (!isBigPicture && desktopMatch && !document.querySelector('.sls-lua-btn[data-sls-appid="' + desktopMatch[2] + '"]')) {
            var productID = desktopMatch[2];
            var cartBtn = null;
            var cartBtns = document.querySelectorAll('.btn_addtocart, .btn_add_to_cart');
            for (var i = 0; i < cartBtns.length; i++) {
                var c = cartBtns[i];
                var link = c.querySelector('a');
                if (!link) continue;
                var hrefLower = link.href.toLowerCase();
                if (hrefLower.indexOf('bundle') !== -1 || hrefLower.indexOf('dlc') !== -1) continue;
                cartBtn = c;
                break;
            }

            var luaBtn;
            if (cartBtn) {
                cartBtn.dataset.slsProcessed = '1';
            }
            luaBtn = document.createElement('div');
            luaBtn.className = 'btn_addtocart';
            luaBtn.innerHTML = '<a class="btn_green_steamui btn_medium" href="javascript:void(0)"><span>Checking Lua...</span></a>';

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

            var header = document.querySelector('div.apphub_HeaderStandardTop') || document.querySelector('.apphub_HeaderStandardTop');
            if (header && header.offsetParent !== null) {
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
                var purchaseArea = document.getElementById('game_area_purchase') || document.querySelector('.game_area_purchase') || document.querySelector('.game_area_comingsoon') || document.querySelector('.game_bg') || document.querySelector('.rightcol');
                if (purchaseArea) {
                    luaBtn.style.float = 'none';
                    luaBtn.style.marginBottom = '15px';
                    purchaseArea.prepend(luaBtn);
                }
            }

            // Register for gamepad navigation
            setTimeout(function() {
                registerSteamFocusNode(luaBtn);
                luaBtn.addEventListener('vgp_onfocus', function() {
                    luaBtn.style.outline = '3px solid #fff';
                    luaBtn.style.outlineOffset = '2px';
                });
                luaBtn.addEventListener('vgp_onblur', function() {
                    luaBtn.style.outline = 'none';
                });
                luaBtn.addEventListener('vgp_onok', function(e) {
                    e.preventDefault(); e.stopPropagation();
                    var link = luaBtn.querySelector('a');
                    if (link) link.click();
                });
            }, 100);
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

        if (observer && document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });
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
    if (document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });
})();
