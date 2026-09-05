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
        { name: 'Ryuu', key: 'sls-ryuu-key' },
        { name: 'DepotBox', key: 'sls-dpbx-key' },
        { name: 'Morrenus', key: 'sls-morr-key' },
        { name: 'Sushi', key: 'sls-sushi-key' },
        { name: 'Spinoza', key: 'sls-spinoza-key' },
        { name: 'TwentyTwo Cloud', key: 'sls-twentytwo-key' }
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
        window.location.hash = 'sls-auth-MORR=' + encodeURIComponent(morr) + '&RYUU=' + encodeURIComponent(ryuu) + '&DPBX=' + encodeURIComponent(dpbx) + '-TS=' + Date.now();
    }

    function sendLuaRequest(appid, providerIndex) {
        var stamp = Date.now();
        if (providerIndex === null) {
            window.location.hash = 'sls-click-auto-' + appid + '-' + getProviderOrder().join(',') + '-' + stamp;
        } else {
            window.location.hash = 'sls-click-provider-' + providerIndex + '-' + appid + '-' + stamp;
        }
    }

    function openLuaProviderConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
        var order = getProviderOrder();
        var rows = '';
        order.forEach(function(providerIndex, position) {
            var provider = luaProviders[providerIndex];
            var apiKey = localStorage.getItem(provider.key) || '';
            rows += '<div data-sls-provider="' + providerIndex + '" style="display:flex;align-items:center;gap:8px;padding:10px 0;border-bottom:1px solid rgba(255,255,255,0.08);">' +
                '<strong style="flex:1;color:#f5f6f8;font-size:13px;">' + provider.name + '</strong>' +
                '<div style="display:flex;align-items:center;gap:6px;min-width:190px;border:1px solid #3b4252;border-radius:5px;padding:3px 6px;">' +
                '<span title="API key" aria-hidden="true" style="color:#a5b4fc;font-size:15px;">&#128273;</span>' +
                '<input data-sls-api-key="' + providerIndex + '" aria-label="API key for ' + escapeHtml(provider.name) + '" value="' + escapeHtml(apiKey) + '" placeholder="API key" type="text" style="min-width:0;flex:1;background:transparent;border:0;outline:0;color:#f5f6f8;padding:5px 2px;font-size:12px;" />' +
                '</div>' +
                '<button data-sls-up="' + providerIndex + '" title="Move provider up" style="margin-right:4px;background:#252a36;border:0;color:#d6d7d9;padding:5px 8px;border-radius:5px;cursor:pointer;"' + (position === 0 ? ' disabled' : '') + '>Up</button>' +
                '<button data-sls-down="' + providerIndex + '" title="Move provider down" style="margin-right:4px;background:#252a36;border:0;color:#d6d7d9;padding:5px 8px;border-radius:5px;cursor:pointer;"' + (position === order.length - 1 ? ' disabled' : '') + '>Down</button>' +
                '<button data-sls-provider-download="' + providerIndex + '" style="margin-right:4px;background:#1a9fff;border:0;color:#fff;padding:5px 9px;border-radius:5px;cursor:pointer;">Download</button>' +
                '</div>';
        });
        overlay.innerHTML = '<div style="background:#161920;border:1px solid rgba(255,255,255,0.1);border-radius:12px;padding:24px;width:auto;max-width:calc(100% - 32px);box-shadow:0 20px 50px rgba(0,0,0,0.6);font-family:Arial,sans-serif;color:#f5f6f8;">' +
            '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:18px;"><div><h2 style="margin:0;font-size:20px;">Lua download provider</h2><p style="margin:5px 0 0;color:#8f98a0;font-size:12px;">AppID: ' + appid + '</p></div><button id="sls-provider-close" style="margin-right:4px;background:transparent;border:0;color:#9ca3af;font-size:20px;cursor:pointer;">X</button></div>' +
            '<div>' + rows + '</div>' +
            '<div style="display:flex;justify-content:flex-end;gap:8px;margin-top:18px;"><button id="sls-auto-search" style="min-width:100%;background:#4f46e5;border:0;color:#fff;padding:9px 14px;border-radius:6px;cursor:pointer;font-weight:600;">Auto Download</button></div>' +
            '</div>';
        document.body.appendChild(overlay);

        function close() { overlay.remove(); }
        document.getElementById('sls-provider-close').onclick = close;
        var cancelButton = document.getElementById('sls-provider-cancel');
        if (cancelButton) cancelButton.onclick = close;
        document.getElementById('sls-auto-search').onclick = function() {
            close();
            sendLuaRequest(appid, null);
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
                var providerIndex = parseInt(button.getAttribute('data-sls-up') || button.getAttribute('data-sls-down'), 10);
                var current = getProviderOrder();
                var from = current.indexOf(providerIndex);
                var to = button.hasAttribute('data-sls-up') ? from - 1 : from + 1;
                if (from < 0 || to < 0 || to >= current.length) return;
                current.splice(from, 1);
                current.splice(to, 0, providerIndex);
                saveProviderOrder(current);
                close();
                openLuaProviderConfig(appid);
            };
        });
        overlay.querySelectorAll('[data-sls-provider-download]').forEach(function(button) {
            button.onclick = function() {
                close();
                sendLuaRequest(appid, parseInt(button.getAttribute('data-sls-provider-download'), 10));
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
                confirmBtn.style.pointerEvents = 'none';

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
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

        var currentMorr = localStorage.getItem('sls-morr-key') || '%MORR_KEY%';
        var currentRyuu = localStorage.getItem('sls-ryuu-key') || '%RYUU_KEY%';
        var currentDpbx = localStorage.getItem('sls-dpbx-key') || '%DPBX_KEY%';

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
            ' #sls-morr:focus, #sls-ryuu:focus, #sls-dpbx:focus { border-color: #6366f1 !important; box-shadow: 0 0 0 2px rgba(99,102,241,0.2) !important; }';

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
            var morr = document.getElementById('sls-morr').value;
            var ryuu = document.getElementById('sls-ryuu').value;
            var dpbx = document.getElementById('sls-dpbx').value;
            localStorage.setItem('sls-morr-key', morr);
            localStorage.setItem('sls-ryuu-key', ryuu);
            localStorage.setItem('sls-dpbx-key', dpbx);
            window.location.hash = 'sls-auth-MORR=' + encodeURIComponent(morr) + '&RYUU=' + encodeURIComponent(ryuu) + '&DPBX=' + encodeURIComponent(dpbx) + '-TS=' + Date.now();
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
        cartBtns.forEach(function(cartBtn) {
            if (cartBtn.dataset.slsProcessed) return;
            if (cartBtn.classList.contains('sls-lua-btn')) return;
            var link = cartBtn.querySelector('a');
            if (!link) return;
            var hrefLower = link.href.toLowerCase();
            if (hrefLower.indexOf('bundle') !== -1 || hrefLower.indexOf('dlc') !== -1) return;
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
                    luaLink.style.pointerEvents = 'none';

                    // Check unlock status via callback server
                    if (appUnlockStatus[productID] !== undefined) {
                        var cached = appUnlockStatus[productID];
                        if (cached && (cached.exists || cached.pending)) {
                            setupRemoveButton(luaLink, luaBtn, productID, cartBtn);
                        } else {
                            setupDownloadButton(luaLink, luaBtn, productID, cartBtn);
                        }
                    } else {
                        // Query the server
                        (function(ll, lb, pid) {
                            fetch('http://127.0.0.1:9001/check?id=' + pid)
                                .then(function(r) { return r.json(); })
                                .then(function(data) {
                                    appUnlockStatus[pid] = data;
                                    var isUnlocked = data.exists || data.pending;
                                    if (isUnlocked) {
                                        setupRemoveButton(ll, lb, pid, cartBtn);
                                    } else {
                                        setupDownloadButton(ll, lb, pid, cartBtn);
                                    }
                                })
                                .catch(function() {
                                    ping('Check failed for ' + pid + ', defaulting to Download');
                                    setupDownloadButton(ll, lb, pid, cartBtn);
                                });
                        })(luaLink, luaBtn, productID);
                    }
                }
                // Settings button
                var setBtn = cartBtn.cloneNode(true);
                setBtn.classList.remove('btn_addtocart', 'btn_add_to_cart');
                setBtn.classList.add('sls-settings-btn');
                setBtn.dataset.slsProcessed = '1';
                setBtn.style.display = 'inline-block';
                setBtn.style.marginRight = '4px';
                setBtn.style.float = 'right';
                var setLink = setBtn.querySelector('a');
                if (setLink) {
                    setLink.href = 'javascript:void(0)';
                    setLink.removeAttribute('id');
                    var spanSet = setLink.querySelector('span');
                    if (spanSet) spanSet.innerText = 'Config';
                    setLink.style.filter = 'hue-rotate(200deg) brightness(1.1)';
                    setLink.onclick = function(e) {
                        e.preventDefault();
                        e.stopPropagation();
                        openLuaProviderConfig(productID);
                    };
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
                    // header.insertBefore(setBtn, luaBtn);
                } else {
                    cartBtn.parentNode.insertBefore(luaBtn, cartBtn.nextSibling);
                    // luaBtn.parentNode.insertBefore(setBtn, luaBtn.nextSibling);
                }
            }
        });
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
