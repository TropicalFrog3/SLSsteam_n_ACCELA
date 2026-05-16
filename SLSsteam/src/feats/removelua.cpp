#include "removelua.hpp"
#include "cdpinject.hpp"
#include "../log.hpp"
#include "../config.hpp"

namespace RemoveLua
{
    static std::string generateRemoveLuaJS()
    {
        return R"JS(
(function() {
    'use strict';
    if (window.__slsRemoveLuaInjectedV5) return;
    window.__slsRemoveLuaInjectedV5 = true;

    function log(m) {
        console.log('[SLS] ' + m);
        fetch('http://127.0.0.1:9001/log?msg=' + encodeURIComponent('[RemoveLua] ' + m)).catch(function(){});
    }

    function applyInvisibilityCloak(id) {
        if (!id || document.getElementById('sls-nuke-' + id)) return;
        log('Applying Invisibility Cloak for AppID: ' + id);
        const nukeCSS = `
            div[class*="LibraryApp"][class*="${id}"], 
            a[href*="/app/${id}"], 
            [data-appid="${id}"],
            [class*="GameListItem"][class*="${id}"],
            [class*="app_${id}"] {
                display: none !important;
                visibility: hidden !important;
                pointer-events: none !important;
                height: 0 !important;
                width: 0 !important;
                margin: 0 !important;
                padding: 0 !important;
                overflow: hidden !important;
            }
        `;
        const style = document.createElement('style');
        style.id = 'sls-nuke-' + id;
        style.textContent = nukeCSS;
        document.head.appendChild(style);
    }

    function extractAppId(manageBtn) {
        var appid = null;
        // 1. Check parent element hierarchy (local context)
        var curr = manageBtn;
        while (curr && curr !== document.body) {
            var cls = (typeof curr.className === 'string') ? curr.className : '';
            var m = cls.match(/\bapp_([0-9]+)\b/);
            if (m) { appid = m[1]; break; }
            var da = curr.getAttribute('data-appid');
            if (da) { appid = da; break; }
            curr = curr.parentElement;
        }

        // 2. Check React internals if local search failed
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

        // 3. Fallback to URL/Hash (last resort)
        if (!appid) {
            var match = window.location.href.match(/\/app\/([0-9]+)/);
            if (match) appid = match[1];
            if (!appid) {
                match = window.location.hash.match(/\/app\/([0-9]+)/);
                if (match) appid = match[1];
            }
        }

        // 4. Fallback to background assets or links
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

    // Track appids confirmed to have NO lua (skip future checks for these)
    var noLuaAppIds = {};
    // Track in-flight /check requests by appid to prevent duplicates
    var pendingChecks = {};

    function checkAndCreateButton(manageContainer, appid, retryCount) {
        if (!retryCount) retryCount = 0;

        // Don't retry forever
        if (retryCount > 5) {
            delete pendingChecks[appid];
            return;
        }

        fetch('http://127.0.0.1:9001/check?id=' + appid)
            .then(function(r) { return r.json(); })
            .then(function(data) {
                delete pendingChecks[appid];

                if (data.exists || data.pending) {
                    // App has lua — create button, do NOT cache (DOM may rebuild)
                    createRemoveButton(manageContainer, appid, data.pending);
                } else {
                    // App has no lua — cache so we don't re-check
                    noLuaAppIds[appid] = true;
                }
            })
            .catch(function() {
                // Server not ready yet — retry with backoff
                var delay = Math.min(1000 * Math.pow(2, retryCount), 8000);
                log('check failed for ' + appid + ', retry #' + (retryCount + 1) + ' in ' + delay + 'ms');
                setTimeout(function() {
                    // Verify the container is still in the DOM before retrying
                    if (manageContainer.parentNode) {
                        checkAndCreateButton(manageContainer, appid, retryCount + 1);
                    } else {
                        delete pendingChecks[appid];
                    }
                }, delay);
            });
    }

    function createRemoveButton(manageContainer, appid, isPending) {
        // Check if button already exists
        var existingBtn = manageContainer.nextSibling;
        if (existingBtn && existingBtn.classList && existingBtn.classList.contains('sls-remove-lua-btn')) {
            if (existingBtn.dataset.slsAppId == appid) return;
            existingBtn.remove();
        }

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
                
                applyInvisibilityCloak(appid);
                
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

        manageContainer.parentNode.insertBefore(removeBtn, manageContainer.nextSibling);
    }

    function addRemoveLuaButton() {
        var btns = document.querySelectorAll('div[aria-label="Manage"]');
        
        btns.forEach(function(manageBtn) {
            var manageContainer = manageBtn.parentNode;
            if (!manageContainer || !manageContainer.parentNode) return;

            var appid = extractAppId(manageBtn);
            if (!appid) return;

            // If a button already exists for this appid on this container, skip
            var existingBtn = manageContainer.nextSibling;
            if (existingBtn && existingBtn.classList && existingBtn.classList.contains('sls-remove-lua-btn')) {
                if (existingBtn.dataset.slsAppId == appid) return;
                existingBtn.remove();
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
)JS";
    }

    bool injectRemoveLuaScript()
    {
        auto pages = CDPInject::fetchPages();
        if (pages.empty()) return false;

        std::string jsCode = generateRemoveLuaJS();

        for (auto& page : pages)
        {
            if (page.webSocketDebuggerUrl.empty()) continue;

            if (page.title == "SharedJSContext")
                continue;

            CDPInject::injectJS(page.webSocketDebuggerUrl, jsCode);
        }

        return true;
    }

    void removeRemoveLuaScript() {}
}
