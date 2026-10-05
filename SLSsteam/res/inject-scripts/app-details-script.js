(function() {
    'use strict';
    if (window.__slsAppDetailsInjected) return;
    window.__slsAppDetailsInjected = true;

    function log(m) {
        console.log('[SLS] ' + m);
        fetch('http://127.0.0.1:9001/log?msg=' + encodeURIComponent('[RemoveLua] ' + m)).catch(function(){});
    }

    function registerSteamFocusNode(element, properties) {
        if (!properties) properties = { focusable: true };
        if (!element.hasAttribute('tabindex')) element.setAttribute('tabindex', '-1');
        element.classList.add('Focusable');
        var parent = element.parentElement;
        var parentNode = null;
        while (parent && parent !== document.body) {
            var fiberKey = Object.keys(parent).find(key => key.startsWith('__reactFiber'));
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

    function trapSteamGamepadModal(overlay, buttons) {
        var activeIdx = 0;
        function updateFocus() {
            for (var i=0; i<buttons.length; i++) {
                if (!buttons[i]) continue;
                if (i === activeIdx) {
                    buttons[i].style.outline = '3px solid #fff';
                    buttons[i].style.outlineOffset = '2px';
                    if (buttons[i].scrollIntoViewIfNeeded) buttons[i].scrollIntoViewIfNeeded();
                    else buttons[i].scrollIntoView({ block: 'nearest', inline: 'nearest' });
                } else {
                    buttons[i].style.outline = 'none';
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
            var startIdx = activeIdx;
            if (dir === 9 || dir === 11) { // Up or Left
                do { activeIdx = (activeIdx > 0) ? activeIdx - 1 : buttons.length - 1; } while (buttons[activeIdx] && buttons[activeIdx].disabled && activeIdx !== startIdx);
            } else if (dir === 10 || dir === 12) { // Down or Right
                do { activeIdx = (activeIdx < buttons.length - 1) ? activeIdx + 1 : 0; } while (buttons[activeIdx] && buttons[activeIdx].disabled && activeIdx !== startIdx);
            }
            updateFocus();
        }
        function okCatcher(e) {
            if (!document.body.contains(overlay)) return;
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            if (buttons[activeIdx] && !buttons[activeIdx].disabled) buttons[activeIdx].click();
        }
        function cancelCatcher(e) {
            if (!document.body.contains(overlay)) return;
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var cancelBtn = overlay.querySelector('#sls-btn-cancel, #sls-prov-cancel, #sls-close-x, #sls-prov-close');
            if (cancelBtn) cancelBtn.click(); else overlay.remove();
        }
        window.addEventListener('vgp_ondirection', gamepadCatcher, true);
        window.addEventListener('vgp_onok', okCatcher, true);
        window.addEventListener('vgp_oncancel', cancelCatcher, true);
    }

    function extractAppId(manageBtn) {
        var appid = null;
        var curr = manageBtn;
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
                var curr = manageBtn;
                while (curr && curr !== document.body && !appid) {
                    for (var key in curr) {
                        if (key.startsWith('__reactInternalInstance$') || key.startsWith('__reactFiber$')) {
                            var fiber = curr[key];
                            while (fiber) {
                                if (fiber.memoizedProps) {
                                    if (fiber.memoizedProps.appid) { appid = String(fiber.memoizedProps.appid); break; }
                                    if (fiber.memoizedProps.appID) { appid = String(fiber.memoizedProps.appID); break; }
                                    if (fiber.memoizedProps.unAppID) { appid = String(fiber.memoizedProps.unAppID); break; }
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
            if (!appid) {
                match = window.location.hash.match(/\/app\/([0-9]+)/);
                if (match) appid = match[1];
            }
        }

        if (!appid) {
            var elWithStyle = document.querySelector('[style*="/assets/"]');
            if (elWithStyle) {
                var m = elWithStyle.style.backgroundImage.match(/\/assets\/([0-9]+)\//);
                if (m) appid = m[1];
            }
        }
        if (!appid) {
            var links = document.querySelectorAll('a[href*="/app/"]');
            for (var i = 0; i < links.length; i++) {
                var m = links[i].href.match(/\/app\/([0-9]+)/);
                if (m) { appid = m[1]; break; }
            }
        }
        return appid;
    }

    var noLuaAppIds = {};
    var pendingChecks = {}; // keyed by appid + '_' + container-random-key

    function checkAndCreateButton(manageContainer, appid, retryCount) {
        if (!retryCount) retryCount = 0;

        var parentNode = manageContainer.parentNode;
        var containerKey = appid + '_' + (parentNode ? parentNode.dataset.slsKey : appid);

        if (retryCount > 5) {
            delete pendingChecks[containerKey];
            return;
        }

        fetch('http://127.0.0.1:9001/check?id=' + appid)
            .then(function(r) { return r.json(); })
            .then(function(data) {
                delete pendingChecks[containerKey];

                if (data.exists || data.pending) {
                    createRemoveButton(manageContainer, appid, data.pending, data.onlineFixInstalled, data.autoCrackInstalled, data.provider);
                } else {
                    noLuaAppIds[appid] = true;
                }
            })
            .catch(function() {
                var delay = Math.min(1000 * Math.pow(2, retryCount), 8000);
                log('check failed for ' + appid + ', retry #' + (retryCount + 1) + ' in ' + delay + 'ms');
                setTimeout(function() {
                    if (manageContainer.parentNode) {
                        checkAndCreateButton(manageContainer, appid, retryCount + 1);
                    } else {
                        delete pendingChecks[containerKey];
                    }
                }, delay);
            });
    }

    function openSlsConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

        var cardHtml = '<div style="background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: \'Motiva Sans\', sans-serif; color: #dcdedf; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;" id="sls-modal-card">' +
            '<!-- Title bar -->' +
            '<div style="display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:20px;">' +
                '<div>' +
                    '<h2 style="margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;">SLS Game Manager</h2>' +
                    '<p style="margin:4px 0 0; font-size:14px; color:#969696;">AppID: ' + appid + '</p>' +
                '</div>' +
                '<button id="sls-close-x" class="sls-btn-close Focusable" tabindex="-1" role="button">&times;</button>' +
            '</div>' +
            '<!-- Administrative Tools Section -->' +
            '<div id="sls-content-admin" style="display:block; margin-bottom:24px;">' +
                '<div style="display:flex; flex-direction:column; gap:8px;">' +
                    '<!-- Verify Files -->' +
                    '<button id="sls-btn-verify" class="sls-btn-action Focusable" tabindex="-1" role="button" style="width:100%;">' +
                        'Verify Files' +
                    '</button>' +
                    '<!-- Online Fix -->' +
                    '<button id="sls-btn-fix" class="sls-btn-action Focusable" tabindex="-1" role="button" style="width:100%;">Checking Fix status...</button>' +
                    '<!-- Auto Crack -->' +
                    '<button id="sls-btn-crack" class="sls-btn-action Focusable" tabindex="-1" role="button" style="width:100%;">Checking Crack status...</button>' +
                '</div>' +
            '</div>' +
            '<!-- Footer Actions -->' +
            '<div style="display:flex; justify-content:flex-end; gap:10px;">' +
                '<button id="sls-btn-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px;">Close</button>' +
            '</div>' +
        '</div>';

        overlay.innerHTML = cardHtml;
        var style = document.createElement('style');
        style.textContent = ' .sls-btn-action { display: flex; justify-content: center; align-items: center; background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); border: none; border-radius: 2px; padding: 8px 16px; font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; height: 48px; box-sizing: border-box; cursor: pointer; text-decoration: none; transition: background 0.1s ease; }' +
            ' .sls-btn-action:hover { background: rgba(172, 178, 201, 0.25); }' +
            ' .sls-btn-action:active { background: rgba(172, 178, 201, 0.30); }' +
            ' .sls-btn-close { background: transparent; border: none; color: #969696; font-size: 24px; cursor: pointer; width: 32px; height: 32px; border-radius: 2px; display: flex; align-items: center; justify-content: center; transition: background 0.1s ease; }' +
            ' .sls-btn-close:hover { background: rgba(172, 178, 201, 0.14); color: #fff; }';

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

        var verifyBtn = document.getElementById('sls-btn-verify');
        var fixBtn = document.getElementById('sls-btn-fix');
        var crackBtn = document.getElementById('sls-btn-crack');
        var closeX = document.getElementById('sls-close-x');
        var cancelBtn = document.getElementById('sls-btn-cancel');

        trapSteamGamepadModal(overlay, [closeX, verifyBtn, fixBtn, crackBtn, cancelBtn]);

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

        verifyBtn.onclick = function(e) {
            e.preventDefault(); e.stopPropagation();
            verifyBtn.innerText = 'Verifying...';
            log('Verify Files clicked for AppID ' + appid);
            fetch('http://127.0.0.1:9001/verify-files?id=' + appid, { mode: 'no-cors' })
                .then(function() {
                    setTimeout(function() { verifyBtn.innerText = 'Verify Files'; }, 3000);
                });
        };

        fetch('http://127.0.0.1:9001/check?id=' + appid)
            .then(function(r) { return r.json(); })
            .then(function(data) {
                function updateFixBtn(installed) {
                    fixBtn.className = 'sls-btn-action Focusable';
                    if (installed) {
                        fixBtn.innerText = 'Remove Online-Fix';
                        fixBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            fixBtn.innerText = 'Removing...';
                            log('Remove Online-Fix clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/remove-fix?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateFixBtn(false); });
                        };
                    } else {
                        fixBtn.innerText = 'Install Online-Fix';
                        fixBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            fixBtn.innerText = 'Installing...';
                            log('Install Online-Fix clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/install-fix?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateFixBtn(true); });
                        };
                    }
                }
                updateFixBtn(data.onlineFixInstalled);

                function updateCrackBtn(installed) {
                    crackBtn.className = 'sls-btn-action Focusable';
                    if (installed) {
                        crackBtn.innerText = 'Remove AutoCrack';
                        crackBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            crackBtn.innerText = 'Removing...';
                            log('Remove AutoCrack clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/remove-crack?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateCrackBtn(false); });
                        };
                    } else {
                        crackBtn.innerText = 'Install AutoCrack';
                        crackBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            crackBtn.innerText = 'Installing...';
                            log('Install AutoCrack clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/install-crack?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateCrackBtn(true); });
                        };
                    }
                }
                updateCrackBtn(data.autoCrackInstalled);
            })
            .catch(function() {
                fixBtn.innerText = 'Failed to load Online-Fix status';
                crackBtn.innerText = 'Failed to load AutoCrack status';
            });
    }

    var luaProviders = [
        { name: 'Ryuu' },
        { name: 'DepotBox' },
        { name: 'HubcapDB' },
        { name: 'Morrenus' },
        { name: 'Sushi' },
        { name: 'Spinoza' },
        { name: 'TwentyTwo Cloud' }
    ];

    function openChangeProviderModal(appid, currentProvider) {
        if (document.getElementById('sls-provider-modal')) return;
        var overlay = document.createElement('div');
        overlay.id = 'sls-provider-modal';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

        var rows = '';
        for (var i = 0; i < luaProviders.length; i++) {
            var isCurrent = currentProvider && currentProvider.toLowerCase().indexOf(luaProviders[i].name.toLowerCase()) !== -1;
            var badgeHtml = isCurrent ? '<span style="margin-left:8px;font-size:12px;padding:2px 6px;background:rgba(172,178,201,0.2);border-radius:2px;color:#dcdedf;">Current</span>' : '';
            var btnStyle = isCurrent
                ? 'background:rgba(172,178,201,0.05);color:#969696;cursor:default;opacity:0.6;'
                : '';
            rows += '<div style="display:flex;align-items:center;justify-content:space-between;padding:12px 14px;margin-bottom:6px;background:rgba(0,0,0,0.2);border:1px solid rgba(172,178,201,0.1);border-radius:2px;">' +
                '<span style="flex:1;color:#dcdedf;font-size:16px;font-weight:400;font-family:\'Motiva Sans\', sans-serif;">' + luaProviders[i].name + badgeHtml + '</span>' +
                '<button data-sls-provider-idx="' + i + '" class="sls-btn-action Focusable" tabindex="-1" role="button" ' + (isCurrent ? 'disabled ' : '') + 'style="height: 36px; padding: 0 16px; ' + btnStyle + '">' + (isCurrent ? 'Active' : 'Use') + '</button>' +
            '</div>';
        }

        var cardHtml = '<div style="background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: \'Motiva Sans\', sans-serif; color: #dcdedf; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;" id="sls-provider-card">' +
            '<div style="display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:20px;">' +
                '<div>' +
                    '<h2 style="margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;">Change Provider</h2>' +
                    '<p style="margin:4px 0 0; font-size:14px; color:#969696;">AppID: ' + appid + '</p>' +
                '</div>' +
                '<button id="sls-prov-close" class="sls-btn-close Focusable" tabindex="-1" role="button">&times;</button>' +
            '</div>' +
            '<div id="sls-provider-list" style="max-height:320px;overflow-y:auto;margin-bottom:16px;">' + rows + '</div>' +
            '<div id="sls-prov-status" style="display:none;text-align:center;padding:12px;font-size:14px;color:#dcdedf;background:rgba(172,178,201,0.1);border-radius:2px;margin-bottom:16px;"></div>' +
            '<div style="display:flex;justify-content:flex-end;gap:10px;">' +
                '<button id="sls-prov-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px;">Close</button>' +
            '</div>' +
        '</div>';

        overlay.innerHTML = cardHtml;

        var style = document.createElement('style');
        style.textContent = ' .sls-btn-action { display: flex; justify-content: center; align-items: center; background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); border: none; border-radius: 2px; padding: 8px 16px; font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; height: 48px; box-sizing: border-box; cursor: pointer; text-decoration: none; transition: background 0.1s ease; }' +
            ' .sls-btn-action:not(:disabled):hover { background: rgba(172, 178, 201, 0.25); }' +
            ' .sls-btn-action:not(:disabled):active { background: rgba(172, 178, 201, 0.30); }' +
            ' .sls-btn-close { background: transparent; border: none; color: #969696; font-size: 24px; cursor: pointer; width: 32px; height: 32px; border-radius: 2px; display: flex; align-items: center; justify-content: center; transition: background 0.1s ease; }' +
            ' .sls-btn-close:hover { background: rgba(172, 178, 201, 0.14); color: #fff; }';
        overlay.appendChild(style);
        document.body.appendChild(overlay);

        requestAnimationFrame(function() {
            overlay.style.opacity = '1';
            var card = document.getElementById('sls-provider-card');
            if (card) { card.style.transform = 'scale(1)'; card.style.opacity = '1'; }
        });

        function closeModal() {
            overlay.style.opacity = '0';
            var card = document.getElementById('sls-provider-card');
            if (card) { card.style.transform = 'scale(0.95)'; card.style.opacity = '0'; }
            setTimeout(function() { overlay.remove(); }, 300);
        }

        document.getElementById('sls-prov-close').onclick = closeModal;
        document.getElementById('sls-prov-cancel').onclick = closeModal;

        var provBtns = overlay.querySelectorAll('[data-sls-provider-idx]');
        
        var modalButtons = [document.getElementById('sls-prov-close')];
        provBtns.forEach(function(b) { modalButtons.push(b); });
        modalButtons.push(document.getElementById('sls-prov-cancel'));
        trapSteamGamepadModal(overlay, modalButtons);
        provBtns.forEach(function(btn) {
            btn.onclick = function(e) {
                e.preventDefault(); e.stopPropagation();
                var idx = btn.getAttribute('data-sls-provider-idx');
                var statusEl = document.getElementById('sls-prov-status');
                statusEl.style.display = 'block';
                statusEl.innerText = 'Switching to ' + luaProviders[idx].name + '...';

                // Disable all buttons
                provBtns.forEach(function(b) { b.disabled = true; b.style.opacity = '0.5'; b.style.cursor = 'default'; });

                fetch('http://127.0.0.1:9001/change-provider?id=' + appid + '&provider=' + idx)
                    .then(function(r) { return r.json(); })
                    .then(function(data) {
                        if (data.success) {
                            statusEl.innerText = 'Download started from ' + luaProviders[idx].name + '. Please wait...';
                            statusEl.style.background = 'rgba(16,185,129,0.1)';
                            statusEl.style.borderColor = 'rgba(16,185,129,0.3)';
                            statusEl.style.color = '#6ee7b7';
                            
                            var pollCount = 0;
                            var lastStatus = '';
                            var timer = setInterval(function() {
                                var btn = document.querySelector('.sls-lua-btn[data-sls-appid="' + appid + '"]');
                                if (!btn) return;
                                var currentStatus = btn.dataset.slsStatus;
                                if (currentStatus && currentStatus !== lastStatus) {
                                    lastStatus = currentStatus;
                                    statusEl.innerText = currentStatus;
                                    
                                    if (/installed!?/i.test(currentStatus)) {
                                        clearInterval(timer);
                                        statusEl.style.color = '#34d399';
                                        var pl = document.querySelector('.sls-provider-label');
                                        if (pl) pl.innerHTML = 'Lua Provider: <span style="color:#a5b4fc;font-weight:600;">' + luaProviders[idx].name + '</span>';
                                        provBtns.forEach(function(b) { b.disabled = false; b.style.opacity = '1'; b.style.cursor = 'pointer'; });
                                    }
                                    else if (/not available|failed|error|invalid/i.test(currentStatus)) {
                                        clearInterval(timer);
                                        statusEl.style.color = '#fca5a5';
                                        statusEl.style.background = 'rgba(239,68,68,0.1)';
                                        statusEl.style.borderColor = 'rgba(239,68,68,0.3)';
                                        provBtns.forEach(function(b) { b.disabled = false; b.style.opacity = '1'; b.style.cursor = 'pointer'; });
                                    }
                                }
                                pollCount++;
                                if (pollCount > 600) clearInterval(timer);
                            }, 500);

                        } else {
                            statusEl.innerText = 'Failed to start download.';
                            statusEl.style.color = '#fca5a5';
                        }
                    })
                    .catch(function() {
                        statusEl.innerText = 'Connection error.';
                        statusEl.style.color = '#fca5a5';
                    });
            };
        });
    }

    function createRemoveButton(manageContainer, appid, isPending, onlineFixInstalled, autoCrackInstalled, provider) {
        var parentNode = manageContainer.parentNode;

        // Remove any existing SLS buttons from this container before re-inserting
        parentNode.querySelectorAll(
            '.sls-remove-lua-btn, .sls-config-btn, .sls-provider-label, .sls-change-provider-btn'
        ).forEach(function(el) { el.remove(); });

        // Detect Steam Deck / Big Picture Mode (GamepadUI) vs Desktop Mode
        var isDesktop = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
        var isDeckOrBigPicture = !isDesktop;
        var buttonMargin = isDeckOrBigPicture ? 'margin-left: 10px;' : 'margin-right: 10px;';
        var buttonHeight = isDeckOrBigPicture ? '48px' : '32px';
        var buttonPadding = isDeckOrBigPicture ? '8px 16px' : '0 16px';

        // --- Steam-native button style ---
        var steamBtnStyle = 'display: flex; align-items: center; justify-content: center; ' +
            'background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); ' +
            'border: none; border-radius: 2px; padding: ' + buttonPadding + '; ' + buttonMargin + ' ' +
            'font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; ' +
            'height: ' + buttonHeight + '; box-sizing: border-box; cursor: pointer; ' +
            'text-decoration: none; transition: background 0.1s ease;';

        function makeSteamButton(className, text, extraAttrs) {
            var btn = document.createElement('div');
            btn.className = className + ' Focusable';
            btn.setAttribute('tabindex', '-1');
            btn.setAttribute('role', 'button');
            btn.dataset.slsAppid = appid;
            btn.dataset.slsAppId = appid;
            btn.style.cssText = steamBtnStyle;
            if (extraAttrs) {
                for (var k in extraAttrs) btn.setAttribute(k, extraAttrs[k]);
            }

            var label = document.createElement('span');
            label.style.cssText = 'white-space: nowrap;';
            label.innerText = text;
            btn.appendChild(label);

            // Gamepad focus styling (match Steam's focus ring)
            btn.addEventListener('vgp_onfocus', function() {
                btn.style.background = 'rgba(172, 178, 201, 0.30)';
                btn.style.outline = '2px solid white';
                btn.style.outlineOffset = '2px';
            });
            btn.addEventListener('vgp_onblur', function() {
                btn.style.background = 'rgba(172, 178, 201, 0.14)';
                btn.style.outline = 'none';
            });

            // Hover styling
            btn.addEventListener('mouseenter', function() {
                btn.style.background = 'rgba(172, 178, 201, 0.25)';
            });
            btn.addEventListener('mouseleave', function() {
                btn.style.background = 'rgba(172, 178, 201, 0.14)';
            });

            return btn;
        }

        // 1. Remove Lua Button
        var removeBtn = makeSteamButton('sls-remove-lua-btn sls-lua-btn', 'Remove Lua');
        var removeBtnLabel = removeBtn.querySelector('span');

        removeBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            removeBtn.click();
        });

        // 2. Config Button
        var configBtn = makeSteamButton('sls-config-btn', 'Config');

        configBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            configBtn.click();
        });

        configBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            openSlsConfig(appid);
        };

        function setRestartState() {
            removeBtnLabel.innerText = 'Restart Steam...';
            removeBtn.onclick = function(e) {
                e.preventDefault(); e.stopPropagation();
                log('Restart requested');
                fetch('http://127.0.0.1:9001/restart', { mode: 'no-cors' }).catch(function(){});
            };
        }

        if (isPending) {
            setRestartState();
        }

        removeBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            
            log('Remove clicked for ' + appid);
            
            var modalOverlay = document.createElement('div');
            modalOverlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
            modalOverlay.innerHTML = '<div style="background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: \'Motiva Sans\', sans-serif; color: #dcdedf;">' +
                '<h2 style="margin:0 0 10px;font-size:22px;font-weight:300;color:#fff;text-transform:uppercase;letter-spacing:1px;">Remove Lua</h2>' +
                '<p style="margin:0 0 24px;font-size:16px;color:#969696;">Remove Lua and Game files for AppID <b>' + appid + '</b>?</p>' +
                '<div style="display:flex;justify-content:flex-end;gap:10px;">' +
                    '<button id="sls-remove-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px;">Cancel</button>' +
                    '<button id="sls-remove-confirm" class="sls-btn-action Focusable" tabindex="-1" role="button" style="padding: 0 24px; height: 36px; background: rgba(220, 38, 38, 0.8); color: #fff;">Remove</button>' +
                '</div>' +
                '<style>' +
                ' .sls-btn-action { display: flex; justify-content: center; align-items: center; background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); border: none; border-radius: 2px; padding: 8px 16px; font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; height: 48px; box-sizing: border-box; cursor: pointer; text-decoration: none; transition: background 0.1s ease; }' +
                ' .sls-btn-action:hover { background: rgba(172, 178, 201, 0.25); }' +
                ' .sls-btn-action:active { background: rgba(172, 178, 201, 0.30); }' +
                ' #sls-remove-confirm:hover { background: rgba(220, 38, 38, 1) !important; }' +
                '</style>' +
            '</div>';
            
            document.body.appendChild(modalOverlay);
            
            var cancelBtn = document.getElementById('sls-remove-cancel');
            var confirmBtn = document.getElementById('sls-remove-confirm');
            trapSteamGamepadModal(modalOverlay, [cancelBtn, confirmBtn]);

            cancelBtn.onclick = function() { modalOverlay.remove(); };
            confirmBtn.onclick = function() {
                confirmBtn.innerText = 'Processing...';
                confirmBtn.style.opacity = '0.5';
                confirmBtn.style.pointerEvents = 'none';
                
                try {
                    if (window.SteamClient && window.SteamClient.Apps && window.SteamClient.Apps.SetAppHidden) {
                        window.SteamClient.Apps.SetAppHidden(appid, true);
                    }
                } catch(e) {}

                fetch('http://127.0.0.1:9001/remove?id=' + appid + '&game=true', { mode: 'no-cors' })
                    .then(function() {
                        setRestartState();
                        modalOverlay.remove();
                    });
            };
        };

        // 3. Change Provider Button
        var changeProvBtn = makeSteamButton('sls-change-provider-btn', 'Change Provider');

        changeProvBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            changeProvBtn.click();
        });

        changeProvBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            openChangeProviderModal(appid, provider || '');
        };

        // 4. Provider label (styled as a passive Steam-style element)
        var providerLabel = document.createElement('div');
        providerLabel.className = 'sls-provider-label';
        providerLabel.style.cssText = 'display: flex; align-items: center; justify-content: center; ' +
            'background: transparent; color: rgb(139, 147, 164); ' +
            'border: none; border-radius: 2px; padding: ' + (isDeckOrBigPicture ? '8px 12px' : '0 12px') + '; ' + buttonMargin + ' ' +
            'font-size: 14px; font-weight: 400; font-family: "Motiva Sans", sans-serif; ' +
            'height: ' + buttonHeight + '; box-sizing: border-box; white-space: nowrap;';
        if (provider) {
            providerLabel.innerHTML = 'Provider: <span style="color:rgb(220, 222, 223);font-weight:500;margin-left:4px;">' + provider + '</span>';
        }

        // Mark this container as injected BEFORE inserting DOM nodes so the
        // MutationObserver callback that fires during insertion sees the guard
        // already set and skips re-entry.
        parentNode.dataset.slsInjected = appid;

        // Insert SLS buttons BEFORE the Manage gear (manageContainer),
        // so they appear first in the row. Reverse insertion order keeps
        // visual order: Provider Label → Remove Lua → Config → Change Provider.
        parentNode.insertBefore(changeProvBtn, parentNode.firstChild);
        parentNode.insertBefore(configBtn, parentNode.firstChild);
        parentNode.insertBefore(removeBtn, parentNode.firstChild);
        if (provider) parentNode.insertBefore(providerLabel, parentNode.firstChild);
        
        // Register for Gamepad Focus
        setTimeout(function() {
            registerSteamFocusNode(removeBtn);
            registerSteamFocusNode(configBtn);
            registerSteamFocusNode(changeProvBtn);
        }, 10);
    }

    function addRemoveLuaButton() {
        var btns = document.querySelectorAll('div[aria-label="Manage"]');
        
        btns.forEach(function(manageBtn) {
            var manageContainer = manageBtn.parentNode;
            if (!manageContainer || !manageContainer.parentNode) return;

            var appid = extractAppId(manageBtn);
            if (!appid) return;

            var parentNode = manageContainer.parentNode;

            // If this exact container is already injected for this appid, skip
            if (parentNode.dataset.slsInjected === appid) return;

            // If we already confirmed this appid has NO lua, skip
            if (noLuaAppIds[appid]) return;

            // If a /check is already in-flight for this appid+container, skip
            var containerKey = appid + '_' + (parentNode.dataset.slsKey || (parentNode.dataset.slsKey = Math.random().toString(36).slice(2)));
            if (pendingChecks[containerKey]) return;
            pendingChecks[containerKey] = true;

            checkAndCreateButton(manageContainer, appid, 0);
        });
    }


    var debounceTimer = null;
    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function() {
            debounceTimer = null;
            addRemoveLuaButton();
        });
    }

    var observer = new MutationObserver(debouncedAddButtons);
    if (document.body) observer.observe(document.body, { childList: true, subtree: true });
    addRemoveLuaButton();
})();