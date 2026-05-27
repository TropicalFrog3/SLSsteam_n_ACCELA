(function() {
    'use strict';
    if (window.__slsAppDetailsInjected) return;
    window.__slsAppDetailsInjected = true;

    function log(m) {
        console.log('[SLS] ' + m);
        fetch('http://127.0.0.1:9001/log?msg=' + encodeURIComponent('[RemoveLua] ' + m)).catch(function(){});
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
    var pendingChecks = {};

    function checkAndCreateButton(manageContainer, appid, retryCount) {
        if (!retryCount) retryCount = 0;

        if (retryCount > 5) {
            delete pendingChecks[appid];
            return;
        }

        fetch('http://127.0.0.1:9001/check?id=' + appid)
            .then(function(r) { return r.json(); })
            .then(function(data) {
                delete pendingChecks[appid];

                if (data.exists || data.pending) {
                    createRemoveButton(manageContainer, appid, data.pending, data.onlineFixInstalled, data.autoCrackInstalled);
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
                        delete pendingChecks[appid];
                    }
                }, delay);
            });
    }

    function openSlsConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

        var cardHtml = '<div style="background: linear-gradient(145deg, #161920 0%, #0d0f14 100%); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 16px; padding: 28px; width: 440px; box-shadow: 0 20px 50px rgba(0,0,0,0.6); font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, Helvetica, Arial, sans-serif; color: #f5f6f8; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;" id="sls-modal-card">' +
            '<!-- Title bar -->' +
            '<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:20px;">' +
                '<div>' +
                    '<h2 style="margin:0; font-size:22px; font-weight:700; background: linear-gradient(90deg, #fff 0%, #a5aab6 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent;">SLS Game Manager</h2>' +
                    '<p style="margin:4px 0 0; font-size:12px; color:#6b7280; font-weight: 500;">AppID: <span style="color:#9ca3af; font-family:monospace;">' + appid + '</span></p>' +
                '</div>' +
                '<button id="sls-close-x" style="background:rgba(255,255,255,0.05); border:none; color:#9ca3af; font-size:20px; cursor:pointer; width:32px; height:32px; border-radius:50%; display:flex; align-items:center; justify-content:center; transition: all 0.2s;">&times;</button>' +
            '</div>' +
            '<!-- Administrative Tools Section -->' +
            '<div id="sls-content-admin" style="display:block; margin-bottom:24px;">' +
                '<div style="display:flex; flex-direction:column; gap:12px;">' +
                    '<!-- Verify Files -->' +
                    '<button id="sls-btn-verify" class="sls-btn-action sls-btn-verify" style="width:100%; box-sizing:border-box;">' +
                        '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="margin-right:8px;"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"></path><polyline points="22 4 12 14.01 9 11.01"></polyline></svg>' +
                        'Verify Files' +
                    '</button>' +
                    '<!-- Online Fix -->' +
                    '<button id="sls-btn-fix" class="sls-btn-action" style="width:100%; box-sizing:border-box;">Checking Fix status...</button>' +
                    '<!-- Auto Crack -->' +
                    '<button id="sls-btn-crack" class="sls-btn-action" style="width:100%; box-sizing:border-box;">Checking Crack status...</button>' +
                '</div>' +
            '</div>' +
            '<!-- Footer Actions -->' +
            '<div style="display:flex; justify-content:flex-end; gap:12px;">' +
                '<button id="sls-btn-cancel" style="background:transparent; border:1px solid rgba(255,255,255,0.1); color:#9ca3af; padding:10px 20px; border-radius:8px; cursor:pointer; font-size:13px; font-weight:600; transition: all 0.2s;">Close</button>' +
            '</div>' +
        '</div>';

        overlay.innerHTML = cardHtml;
        var style = document.createElement('style');
        style.textContent = ' .sls-btn-action { display: flex; justify-content: center; align-items: center; border: none; color: #fff; padding: 11px; border-radius: 8px; font-size: 13px; font-weight: 600; cursor: pointer; transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1); box-shadow: 0 2px 4px rgba(0,0,0,0.15); }' +
            ' .sls-btn-action:hover { transform: translateY(-1.5px); box-shadow: 0 6px 16px rgba(0,0,0,0.3); filter: brightness(1.15); }' +
            ' .sls-btn-action:active { transform: translateY(0); }' +
            ' .sls-btn-verify { background: linear-gradient(135deg, #6366f1 0%, #4f46e5 100%); }' +
            ' .sls-btn-install { background: linear-gradient(135deg, #0ea5e9 0%, #0284c7 100%); }' +
            ' .sls-btn-remove { background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%); }' +
            ' .sls-btn-crack-install { background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%); }' +
            ' #sls-close-x:hover { background: rgba(255,255,255,0.1) !important; color: #fff !important; }' +
            ' #sls-btn-cancel:hover { background: rgba(255,255,255,0.03) !important; color: #fff !important; border-color: rgba(255,255,255,0.2) !important; }';

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
                    fixBtn.className = 'sls-btn-action';
                    if (installed) {
                        fixBtn.innerText = 'Remove Online-Fix';
                        fixBtn.classList.add('sls-btn-remove');
                        fixBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            fixBtn.innerText = 'Removing...';
                            log('Remove Online-Fix clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/remove-fix?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateFixBtn(false); });
                        };
                    } else {
                        fixBtn.innerText = 'Install Online-Fix';
                        fixBtn.classList.add('sls-btn-install');
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
                    crackBtn.className = 'sls-btn-action';
                    if (installed) {
                        crackBtn.innerText = 'Remove AutoCrack';
                        crackBtn.classList.add('sls-btn-remove');
                        crackBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            crackBtn.innerText = 'Removing...';
                            log('Remove AutoCrack clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/remove-crack?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateCrackBtn(false); });
                        };
                    } else {
                        crackBtn.innerText = 'Install AutoCrack';
                        crackBtn.classList.add('sls-btn-crack-install');
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

    function createRemoveButton(manageContainer, appid, isPending, onlineFixInstalled, autoCrackInstalled) {
        var parentNode = manageContainer.parentNode;
        var existingBtn = null;
        var existingConfigBtn = null;
        var existingFixInstallBtn = null;
        var sibling = manageContainer.nextSibling;
        while (sibling) {
            if (sibling.classList) {
                if (sibling.classList.contains('sls-remove-lua-btn')) {
                    existingBtn = sibling;
                } else if (sibling.classList.contains('sls-config-btn')) {
                    existingConfigBtn = sibling;
                } else if (sibling.classList.contains('sls-fix-install-btn')) {
                    existingFixInstallBtn = sibling;
                } else {
                    if (sibling.classList.contains('sls-verify-btn') || 
                        sibling.classList.contains('sls-fix-btn') || 
                        sibling.classList.contains('sls-crack-btn')) {
                        var toRemove = sibling;
                        sibling = sibling.nextSibling;
                        toRemove.remove();
                        continue;
                    }
                }
            }
            sibling = sibling.nextSibling;
        }

        if (existingBtn) {
            existingBtn.remove();
        }
        if (existingConfigBtn) {
            existingConfigBtn.remove();
        }
        if (existingFixInstallBtn) {
            existingFixInstallBtn.remove();
        }

        // 1. Remove Lua Button
        var removeBtn = document.createElement('div');
        removeBtn.className = 'sls-remove-lua-btn';
        removeBtn.dataset.slsAppId = appid;
        removeBtn.style.display = 'inline-block';
        removeBtn.style.marginLeft = '8px';
        
        var luaLink = document.createElement('a');
        luaLink.href = 'javascript:void(0)';
        luaLink.style.cssText = 'display: inline-block; background: linear-gradient(to right, #75b022 5%, #588a1b 95%); border-radius: 2px; padding: 1px; cursor: pointer; text-decoration: none; filter: hue-rotate(110deg) brightness(1.2); box-shadow: 0 1px 3px rgba(0,0,0,0.4);';
        
        var luaSpan = document.createElement('span');
        luaSpan.style.cssText = 'display: block; background: transparent; padding: 0 15px; font-size: 15px; line-height: 30px; color: #d2efa9; text-shadow: 1px 1px 2px rgba(0,0,0,0.3); font-family: "Motiva Sans", sans-serif;';
        luaSpan.innerText = 'Remove Lua';
        
        luaLink.appendChild(luaSpan);
        removeBtn.appendChild(luaLink);

        // 2. Config Button
        var configBtn = document.createElement('div');
        configBtn.className = 'sls-config-btn';
        configBtn.dataset.slsAppId = appid;
        configBtn.style.display = 'inline-block';
        configBtn.style.marginLeft = '8px';
        
        var configLink = document.createElement('a');
        configLink.href = 'javascript:void(0)';
        configLink.style.cssText = 'display: inline-block; background: linear-gradient(to right, #75b022 5%, #588a1b 95%); border-radius: 2px; padding: 1px; cursor: pointer; text-decoration: none; filter: hue-rotate(200deg) brightness(1.1); box-shadow: 0 1px 3px rgba(0,0,0,0.4);';
        
        var configSpan = document.createElement('span');
        configSpan.style.cssText = 'display: block; background: transparent; padding: 0 15px; font-size: 15px; line-height: 30px; color: #d2efa9; text-shadow: 1px 1px 2px rgba(0,0,0,0.3); font-family: "Motiva Sans", sans-serif;';
        configSpan.innerText = 'Config';
        
        configLink.appendChild(configSpan);
        configBtn.appendChild(configLink);

        configLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            openSlsConfig(appid);
        };

        // 3. Fix Install Button
        var fixInstallBtn = document.createElement('div');
        fixInstallBtn.className = 'sls-fix-install-btn';
        fixInstallBtn.dataset.slsAppId = appid;
        fixInstallBtn.style.display = 'inline-block';
        fixInstallBtn.style.marginLeft = '8px';
        
        var fixInstallLink = document.createElement('a');
        fixInstallLink.href = 'javascript:void(0)';
        fixInstallLink.style.cssText = 'display: inline-block; background: linear-gradient(to right, #75b022 5%, #588a1b 95%); border-radius: 2px; padding: 1px; cursor: pointer; text-decoration: none; filter: hue-rotate(280deg) brightness(1.2); box-shadow: 0 1px 3px rgba(0,0,0,0.4);';
        
        var fixInstallSpan = document.createElement('span');
        fixInstallSpan.style.cssText = 'display: block; background: transparent; padding: 0 15px; font-size: 15px; line-height: 30px; color: #d2efa9; text-shadow: 1px 1px 2px rgba(0,0,0,0.3); font-family: "Motiva Sans", sans-serif;';
        fixInstallSpan.innerText = 'Fix Install';
        
        fixInstallLink.appendChild(fixInstallSpan);
        fixInstallBtn.appendChild(fixInstallLink);

        fixInstallLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            
            log('Fix Install clicked for ' + appid);
            
            var modalOverlay = document.createElement('div');
            modalOverlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(5px);';
            modalOverlay.innerHTML = '<div style="background:#1a1c23;border:1px solid #2a2d36;border-radius:12px;padding:30px;width:400px;box-shadow:0 15px 30px rgba(0,0,0,0.5);font-family:Inter,sans-serif;color:#fff;text-align:center;">' +
                '<h2 style="margin:0 0 10px;font-size:20px;font-weight:600;color:#e8e9eb;">Fix Install</h2>' +
                '<p style="margin:0 0 20px;font-size:13px;color:#8a8d96;">Remove AppID <b>' + appid + '</b> from the installed list in .SLSsteam.json?</p>' +
                '<div style="display:flex;justify-content:center;gap:10px;">' +
                    '<button id="sls-fix-cancel" style="background:transparent;border:1px solid #333640;color:#e8e9eb;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;">Cancel</button>' +
                    '<button id="sls-fix-confirm" style="background:#0ea5e9;border:none;color:#fff;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;box-shadow:0 4px 10px rgba(14,165,233,0.3);">Confirm</button>' +
                '</div>' +
            '</div>';
            
            document.body.appendChild(modalOverlay);

            document.getElementById('sls-fix-cancel').onclick = function() { modalOverlay.remove(); };
            document.getElementById('sls-fix-confirm').onclick = function() {
                var confirmBtn = document.getElementById('sls-fix-confirm');
                confirmBtn.innerText = 'Processing...';
                confirmBtn.style.opacity = '0.5';
                confirmBtn.style.pointerEvents = 'none';
                
                fetch('http://127.0.0.1:9001/fix-install?id=' + appid, { mode: 'no-cors' })
                    .then(function() {
                        fixInstallSpan.innerText = 'Fixed!';
                        fixInstallLink.style.pointerEvents = 'none';
                        fixInstallLink.style.opacity = '0.6';
                        modalOverlay.remove();
                    })
                    .catch(function() {
                        confirmBtn.innerText = 'Failed';
                    });
            };
        };

        function setRestartState() {
            luaSpan.innerText = 'Restart Steam...';
            luaLink.onclick = function(e) {
                e.preventDefault(); e.stopPropagation();
                log('Restart requested');
                fetch('http://127.0.0.1:9001/restart', { mode: 'no-cors' }).catch(function(){});
            };
        }

        if (isPending) {
            setRestartState();
        }

        luaLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            
            log('Remove clicked for ' + appid);
            
            var modalOverlay = document.createElement('div');
            modalOverlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(5px);';
            modalOverlay.innerHTML = '<div style="background:#1a1c23;border:1px solid #2a2d36;border-radius:12px;padding:30px;width:400px;box-shadow:0 15px 30px rgba(0,0,0,0.5);font-family:Inter,sans-serif;color:#fff;text-align:center;">' +
                '<h2 style="margin:0 0 10px;font-size:20px;font-weight:600;color:#e8e9eb;">Remove Lua</h2>' +
                '<p style="margin:0 0 20px;font-size:13px;color:#8a8d96;">Remove Lua and Game files for AppID <b>' + appid + '</b>?</p>' +
                '<div style="display:flex;justify-content:center;gap:10px;">' +
                    '<button id="sls-remove-cancel" style="background:transparent;border:1px solid #333640;color:#e8e9eb;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;">Cancel</button>' +
                    '<button id="sls-remove-confirm" style="background:#ff4d4d;border:none;color:#fff;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;box-shadow:0 4px 10px rgba(255,77,77,0.3);">Remove</button>' +
                '</div>' +
            '</div>';
            
            document.body.appendChild(modalOverlay);

            document.getElementById('sls-remove-cancel').onclick = function() { modalOverlay.remove(); };
            document.getElementById('sls-remove-confirm').onclick = function() {
                var confirmBtn = document.getElementById('sls-remove-confirm');
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

        parentNode.insertBefore(removeBtn, manageContainer.nextSibling);
        parentNode.insertBefore(configBtn, removeBtn.nextSibling);
        parentNode.insertBefore(fixInstallBtn, configBtn.nextSibling);
    }

    function addRemoveLuaButton() {
        var btns = document.querySelectorAll('div[aria-label="Manage"]');
        
        btns.forEach(function(manageBtn) {
            var manageContainer = manageBtn.parentNode;
            if (!manageContainer || !manageContainer.parentNode) return;

            var appid = extractAppId(manageBtn);
            if (!appid) return;

            // If buttons already exist for this appid on this container, skip
            var parentNode = manageContainer.parentNode;
            var existingBtn = null;
            var existingConfigBtn = null;
            var existingFixInstallBtn = null;
            var sibling = manageContainer.nextSibling;
            while (sibling) {
                if (sibling.classList) {
                    if (sibling.classList.contains('sls-remove-lua-btn')) {
                        existingBtn = sibling;
                    } else if (sibling.classList.contains('sls-config-btn')) {
                        existingConfigBtn = sibling;
                    } else if (sibling.classList.contains('sls-fix-install-btn')) {
                        existingFixInstallBtn = sibling;
                    }
                }
                sibling = sibling.nextSibling;
            }
            if (existingBtn && existingConfigBtn && existingFixInstallBtn && existingBtn.dataset.slsAppId == appid) {
                return;
            }

            // If we already confirmed this appid has NO lua, skip
            if (noLuaAppIds[appid]) return;

            // If a /check is already in-flight for this appid, skip (prevents double requests)
            if (pendingChecks[appid]) return;
            pendingChecks[appid] = true;

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