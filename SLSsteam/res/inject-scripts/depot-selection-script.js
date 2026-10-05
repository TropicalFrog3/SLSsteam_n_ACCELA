(function() {
    'use strict';
    
    // Prevent double injection if somehow triggered twice rapidly
    if (document.getElementById('sls-depot-overlay-modal')) return;

    // Never inject into supernav dropdowns, context menus, or headless contexts
    if (document.title.indexOf('Supernav') !== -1 ||
        (document.title.indexOf('Menu') !== -1 && document.title.indexOf('MainMenu_uid') === -1) ||
        document.title === 'SharedJSContext') return;

    var appid = "%APPID%";

    var activeGamepadCleanup = null;
    function setupGamepadTrap(buttons, startR, startC) {
        if (activeGamepadCleanup) {
            activeGamepadCleanup();
            activeGamepadCleanup = null;
        }
        activeGamepadCleanup = trapSteamGamepadModal(overlay, buttons, startR, startC);
    }

    function trapSteamGamepadModal(overlay, buttons, initialActiveR, initialActiveC) {
        var isGrid = Array.isArray(buttons[0]);
        var grid = isGrid ? buttons : buttons.map(function(b) { return [b]; });
        var activeR = (initialActiveR !== undefined) ? initialActiveR : 0;
        var activeC = (initialActiveC !== undefined) ? initialActiveC : 0;

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
        updateFocus();
        setTimeout(updateFocus, 50);
        
        function gamepadCatcher(e) {
            if (!document.body.contains(overlay)) {
                cleanup();
                return;
            }
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var dir = e.detail ? e.detail.button : null;
            
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
            if (!btn || btn.disabled) return;
            // If this is a checkbox (or its parent label), toggle it
            if (btn.tagName === 'INPUT' && btn.type === 'checkbox') {
                btn.checked = !btn.checked;
                btn.dispatchEvent(new Event('change', { bubbles: true }));
            } else if (btn.tagName === 'LABEL' && btn.querySelector('input[type="checkbox"]')) {
                var cb = btn.querySelector('input[type="checkbox"]');
                cb.checked = !cb.checked;
                cb.dispatchEvent(new Event('change', { bubbles: true }));
            } else {
                btn.click();
            }
        }
        function cancelCatcher(e) {
            if (!document.body.contains(overlay)) return;
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var closeBtn = overlay.querySelector('#sls-depot-close-x, #sls-depot-btn-cancel');
            if (closeBtn) closeBtn.click(); else overlay.remove();
        }
        window.addEventListener('vgp_ondirection', gamepadCatcher, true);
        window.addEventListener('vgp_onok', okCatcher, true);
        window.addEventListener('vgp_oncancel', cancelCatcher, true);

        function cleanup() {
            window.removeEventListener('vgp_ondirection', gamepadCatcher, true);
            window.removeEventListener('vgp_onok', okCatcher, true);
            window.removeEventListener('vgp_oncancel', cancelCatcher, true);
            for (var r = 0; r < grid.length; r++) {
                for (var c = 0; c < grid[r].length; c++) {
                    if (grid[r][c]) grid[r][c].style.outline = 'none';
                }
            }
        }
        return cleanup;
    }

    var overlay = document.createElement('div');
    overlay.id = 'sls-depot-overlay-modal';
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:2147483647;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:1;';

    var cardHtml = '<div style="background: linear-gradient(145deg, #161920 0%, #0d0f14 100%); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 16px; padding: 28px; width: 480px; max-height: 80vh; display: flex; flex-direction: column; box-shadow: 0 20px 50px rgba(0,0,0,0.6); font-family: -apple-system, BlinkMacSystemFont, \'Segoe UI\', Roboto, Helvetica, Arial, sans-serif; color: #f5f6f8; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;" id="sls-depot-modal-card">' +
        '<!-- Title bar -->' +
        '<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:20px; flex-shrink: 0;">' +
            '<div>' +
                '<h2 style="margin:0; font-size:22px; font-weight:700; background: linear-gradient(90deg, #fff 0%, #a5aab6 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent;">Select Depots</h2>' +
                '<p style="margin:4px 0 0; font-size:12px; color:#6b7280; font-weight: 500;">AppID: <span style="color:#9ca3af; font-family:monospace;">' + appid + '</span></p>' +
            '</div>' +
            '<button id="sls-depot-close-x" style="background:rgba(255,255,255,0.05); border:none; color:#9ca3af; font-size:20px; cursor:pointer; width:32px; height:32px; border-radius:50%; display:flex; align-items:center; justify-content:center; transition: all 0.2s;">&times;</button>' +
        '</div>' +
        '<!-- Content -->' +
        '<div id="sls-depot-content" style="flex-grow: 1; overflow-y: auto; margin-bottom: 24px; padding-right: 8px;">' +
            '<div class="sls-loading-header" id="sls-depot-loading-status">' +
                '<div class="sls-spinner"></div>' +
                '<div class="sls-loading-text-wrap">' +
                    '<span class="sls-loading-title">Loading depots & details…</span>' +
                    '<span class="sls-loading-subtitle">Fetching available content from Steam</span>' +
                '</div>' +
            '</div>' +
            '<div class="sls-skeleton-container" id="sls-depot-skeleton-list">' +
                '<div class="sls-skeleton-item">' +
                    '<div class="sls-skeleton-checkbox"></div>' +
                    '<div class="sls-skeleton-info">' +
                        '<div class="sls-skeleton-line-1" style="width: 58%;"></div>' +
                        '<div class="sls-skeleton-line-2" style="width: 90%;"></div>' +
                    '</div>' +
                '</div>' +
                '<div class="sls-skeleton-item">' +
                    '<div class="sls-skeleton-checkbox"></div>' +
                    '<div class="sls-skeleton-info">' +
                        '<div class="sls-skeleton-line-1" style="width: 44%;"></div>' +
                        '<div class="sls-skeleton-line-2" style="width: 80%;"></div>' +
                    '</div>' +
                '</div>' +
                '<div class="sls-skeleton-item" style="opacity: 0.65;">' +
                    '<div class="sls-skeleton-checkbox"></div>' +
                    '<div class="sls-skeleton-info">' +
                        '<div class="sls-skeleton-line-1" style="width: 68%;"></div>' +
                        '<div class="sls-skeleton-line-2" style="width: 75%;"></div>' +
                    '</div>' +
                '</div>' +
                '<div class="sls-skeleton-item" style="opacity: 0.35;">' +
                    '<div class="sls-skeleton-checkbox"></div>' +
                    '<div class="sls-skeleton-info">' +
                        '<div class="sls-skeleton-line-1" style="width: 50%;"></div>' +
                        '<div class="sls-skeleton-line-2" style="width: 60%;"></div>' +
                    '</div>' +
                '</div>' +
            '</div>' +
        '</div>' +
        '<!-- Footer Actions -->' +
        '<div style="display:flex; justify-content:flex-end; gap:12px; flex-shrink: 0;">' +
            '<button id="sls-depot-btn-cancel" style="background:transparent; border:1px solid rgba(255,255,255,0.1); color:#9ca3af; padding:10px 20px; border-radius:8px; cursor:pointer; font-size:13px; font-weight:600; transition: all 0.2s;">Cancel</button>' +
            '<button id="sls-depot-btn-download" style="background: linear-gradient(135deg, #0ea5e9 0%, #0284c7 100%); border:none; color:#fff; padding:10px 20px; border-radius:8px; cursor:pointer; font-size:13px; font-weight:600; box-shadow: 0 2px 4px rgba(0,0,0,0.15); transition: all 0.2s cubic-bezier(0.16, 1, 0.3, 1); opacity: 0.5; pointer-events: none;">Download</button>' +
        '</div>' +
    '</div>';

    overlay.innerHTML = cardHtml;
    
    var style = document.createElement('style');
    style.textContent = 
        ' @keyframes sls-spin { to { transform: rotate(360deg); } }' +
        ' @keyframes sls-shimmer { 0% { background-position: -468px 0; } 100% { background-position: 468px 0; } }' +
        ' .sls-loading-header { display: flex; align-items: center; gap: 14px; padding: 12px 14px; background: rgba(14, 165, 233, 0.06); border: 1px solid rgba(14, 165, 233, 0.2); border-radius: 10px; margin-bottom: 14px; }' +
        ' .sls-spinner { width: 22px; height: 22px; border-radius: 50%; border: 2.5px solid rgba(255, 255, 255, 0.1); border-top-color: #0ea5e9; border-right-color: #38bdf8; animation: sls-spin 0.75s linear infinite; flex-shrink: 0; }' +
        ' .sls-loading-text-wrap { display: flex; flex-direction: column; }' +
        ' .sls-loading-title { font-size: 13px; font-weight: 600; color: #f1f5f9; display: block; }' +
        ' .sls-loading-subtitle { font-size: 11px; color: #94a3b8; display: block; margin-top: 2px; }' +
        ' .sls-resolving-pill { display: inline-flex; align-items: center; gap: 8px; padding: 4px 10px; border-radius: 12px; background: rgba(14, 165, 233, 0.08); border: 1px solid rgba(14, 165, 233, 0.2); color: #38bdf8; font-size: 11px; font-weight: 500; margin-bottom: 12px; transition: opacity 0.3s ease; }' +
        ' .sls-mini-spinner { width: 12px; height: 12px; border-radius: 50%; border: 1.5px solid rgba(56, 189, 248, 0.2); border-top-color: #38bdf8; animation: sls-spin 0.7s linear infinite; flex-shrink: 0; }' +
        ' .sls-skeleton-container { display: flex; flex-direction: column; }' +
        ' .sls-skeleton-item { display: flex; align-items: flex-start; padding: 12px; border-radius: 8px; background: rgba(255,255,255,0.02); border: 1px solid rgba(255,255,255,0.04); margin-bottom: 8px; }' +
        ' .sls-skeleton-checkbox { width: 18px; height: 18px; border-radius: 4px; background: #2a2d36; margin-right: 12px; margin-top: 2px; flex-shrink: 0; }' +
        ' .sls-skeleton-info { display: flex; flex-direction: column; flex-grow: 1; }' +
        ' .sls-skeleton-line-1, .sls-skeleton-line-2 { background: #2a2d36; background-image: linear-gradient(to right, #2a2d36 0%, #3a3d46 20%, #2a2d36 40%, #2a2d36 100%); background-repeat: no-repeat; background-size: 800px 100%; animation: sls-shimmer 1.5s linear infinite forwards; border-radius: 4px; }' +
        ' .sls-skeleton-line-1 { height: 14px; width: 60%; margin-bottom: 8px; }' +
        ' .sls-skeleton-line-2 { height: 12px; width: 100%; }' +
        ' #sls-depot-close-x:hover { background: rgba(255,255,255,0.1) !important; color: #fff !important; }' +
        ' #sls-depot-btn-cancel:hover { background: rgba(255,255,255,0.03) !important; color: #fff !important; border-color: rgba(255,255,255,0.2) !important; }' +
        ' #sls-depot-btn-download:hover { transform: translateY(-1.5px); box-shadow: 0 6px 16px rgba(0,0,0,0.3); filter: brightness(1.15); }' +
        ' #sls-depot-btn-download:active { transform: translateY(0); }' +
        ' .sls-depot-item { display: flex; align-items: flex-start; padding: 12px; border-radius: 8px; background: rgba(255,255,255,0.02); border: 1px solid rgba(255,255,255,0.04); margin-bottom: 8px; cursor: pointer; transition: all 0.2s; }' +
        ' .sls-depot-item:hover { background: rgba(255,255,255,0.05); border-color: rgba(255,255,255,0.1); }' +
        ' .sls-depot-checkbox { -webkit-appearance: none; appearance: none; width: 18px; height: 18px; border: 2px solid #4b5563; border-radius: 4px; outline: none; margin: 0 12px 0 0; position: relative; cursor: pointer; transition: all 0.2s; flex-shrink: 0; margin-top: 2px; }' +
        ' .sls-depot-checkbox:checked { background: #0ea5e9; border-color: #0ea5e9; }' +
        ' .sls-depot-checkbox:checked::after { content: ""; position: absolute; left: 5px; top: 1px; width: 4px; height: 9px; border: solid white; border-width: 0 2px 2px 0; transform: rotate(45deg); }' +
        ' .sls-depot-info { display: flex; flex-direction: column; flex-grow: 1; }' +
        ' .sls-depot-name { font-size: 14px; font-weight: 500; color: #e5e7eb; margin-bottom: 4px; word-break: break-word; }' +
        ' .sls-depot-meta { font-size: 12px; color: #8a8d96; display: flex; justify-content: space-between; }' +
        ' /* Custom scrollbar */' +
        ' #sls-depot-content::-webkit-scrollbar { width: 6px; }' +
        ' #sls-depot-content::-webkit-scrollbar-track { background: transparent; }' +
        ' #sls-depot-content::-webkit-scrollbar-thumb { background: rgba(255,255,255,0.1); border-radius: 3px; }' +
        ' #sls-depot-content::-webkit-scrollbar-thumb:hover { background: rgba(255,255,255,0.2); }' +
        ' /* OS badges */' +
        ' .sls-os-badge { display: inline-block; font-size: 10px; font-weight: 700; padding: 1px 6px; border-radius: 4px; margin-left: 8px; vertical-align: middle; text-transform: uppercase; letter-spacing: 0.5px; }' +
        ' .sls-os-badge.sls-os-windows { background: rgba(56, 189, 248, 0.15); color: #38bdf8; border: 1px solid rgba(56, 189, 248, 0.25); }' +
        ' .sls-os-badge.sls-os-linux { background: rgba(251, 191, 36, 0.15); color: #fbbf24; border: 1px solid rgba(251, 191, 36, 0.25); }' +
        ' .sls-os-badge.sls-os-macos { background: rgba(156, 163, 175, 0.15); color: #9ca3af; border: 1px solid rgba(156, 163, 175, 0.25); }';
    overlay.appendChild(style);
    document.body.appendChild(overlay);

    setTimeout(function() {
        overlay.style.opacity = '1';
        var card = document.getElementById('sls-depot-modal-card');
        if (card) {
            card.style.transform = 'scale(1)';
            card.style.opacity = '1';
        }
    }, 10);

    var closeX = document.getElementById('sls-depot-close-x');
    var cancelBtn = document.getElementById('sls-depot-btn-cancel');
    var downloadBtn = document.getElementById('sls-depot-btn-download');
    var contentDiv = document.getElementById('sls-depot-content');

    // Focus cancelBtn immediately so controller and keyboard navigation work during loading
    setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);

    function formatBytes(bytes, decimals) {
        if(bytes == 0) return '0 Bytes';
        if(decimals === undefined) decimals = 2;
        var k = 1024,
            dm = decimals < 0 ? 0 : decimals,
            sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'],
            i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
    }

    function getOsInfo(oslist) {
        if (!oslist) return null;
        var os = oslist.toLowerCase();
        if (os.indexOf('windows') !== -1) return { label: 'Windows', cls: 'sls-os-windows' };
        if (os.indexOf('linux') !== -1) return { label: 'Linux', cls: 'sls-os-linux' };
        if (os.indexOf('macos') !== -1 || os.indexOf('osx') !== -1) return { label: 'macOS', cls: 'sls-os-macos' };
        return null;
    }

    // Returns an HTML string for an OS badge (used during initial render)
    function buildOsBadge(oslist) {
        var info = getOsInfo(oslist);
        if (!info) return '';
        return ' <span class="sls-os-badge ' + info.cls + '">' + info.label + '</span>';
    }

    // Returns a DOM element for an OS badge (used during async updates)
    function buildOsBadgeElement(oslist) {
        var info = getOsInfo(oslist);
        if (!info) return null;
        var el = document.createElement('span');
        el.className = 'sls-os-badge ' + info.cls;
        el.innerText = info.label;
        return el;
    }

    function closeAndCancel() {
        if (activeGamepadCleanup) activeGamepadCleanup();
        overlay.style.pointerEvents = 'none'; // Prevent double clicks
        fetch('http://127.0.0.1:9001/cancel-download?id=' + appid, { mode: 'no-cors' }).catch(function(){});
        
        overlay.style.opacity = '0';
        var card = document.getElementById('sls-depot-modal-card');
        if (card) {
            card.style.transform = 'scale(0.95)';
            card.style.opacity = '0';
        }
        setTimeout(function() { overlay.remove(); }, 300);
    }
    
    function closeSuccess() {
        if (activeGamepadCleanup) activeGamepadCleanup();
        overlay.style.pointerEvents = 'none'; // Prevent double clicks
        overlay.style.opacity = '0';
        var card = document.getElementById('sls-depot-modal-card');
        if (card) {
            card.style.transform = 'scale(0.95)';
            card.style.opacity = '0';
        }
        setTimeout(function() { overlay.remove(); }, 300);
    }

    closeX.onclick = closeAndCancel;
    cancelBtn.onclick = closeAndCancel;

    // Fetch depots
    fetch('http://127.0.0.1:9001/get-depots?id=' + appid)
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (!data.success) {
                contentDiv.innerHTML = '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#ef4444; font-size: 14px;">Error: ' + data.message + '</div>';
                setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);
                return;
            }

            if (!data.depots || data.depots.length === 0) {
                contentDiv.innerHTML = '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#f59e0b; font-size: 14px;">No depots found in Lua plugin.</div>';
                setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);
                return;
            }

            var html = '';
            for (var i = 0; i < data.depots.length; i++) {
                var depot = data.depots[i];
                var sizeStr = depot.size ? formatBytes(parseInt(depot.size, 10)) : 'Unknown size';
                
                var displayName = depot.name;
                if (!displayName || displayName.indexOf('Depot ') === 0) {
                    var depotIdNum = parseInt(depot.id, 10);
                    var appIdNum = parseInt(appid, 10);
                    // Depots within appId to appId+10 without a specific name are base game content
                    // (covers multi-OS depots like appId+1=win, +2=mac, +3=linux)
                    if (depotIdNum >= appIdNum && depotIdNum <= appIdNum + 10) {
                        displayName = 'Base Game Content';
                    } else {
                        displayName = 'Additional Content (' + depot.id + ')';
                    }
                }

                var osBadge = '';
                if (depot.os) {
                    osBadge = buildOsBadge(depot.os);
                }
                
                html += '<label class="sls-depot-item">' +
                    '<input type="checkbox" class="sls-depot-checkbox" value="' + depot.id + '"' + (depot.os ? ' data-os="' + depot.os + '"' : '') + '>' +
                    '<div class="sls-depot-info">' +
                        '<div class="sls-depot-name">' + displayName + osBadge + '</div>' +
                        '<div class="sls-depot-meta">' +
                            '<span>ID: ' + depot.id + '</span>' +
                            '<span>' + sizeStr + '</span>' +
                        '</div>' +
                    '</div>' +
                '</label>';
            }

            // Check if extra metadata needs to be resolved from SteamCMD
            var tempDiv = document.createElement('div');
            tempDiv.innerHTML = html;
            var tempItems = tempDiv.querySelectorAll('.sls-depot-item');
            var needsSteamCmdFetch = false;
            for (var l = 0; l < tempItems.length; l++) {
                var el = tempItems[l];
                var nameEl = el.querySelector('.sls-depot-name');
                var sizeSpan = el.querySelector('.sls-depot-meta').querySelectorAll('span')[1];
                var cb = el.querySelector('.sls-depot-checkbox');
                var hasOs = cb.getAttribute('data-os');
                if (nameEl.innerText.indexOf('Additional Content') === 0 || sizeSpan.innerText === 'Unknown size' || !hasOs) {
                    needsSteamCmdFetch = true;
                    break;
                }
            }

            var resolvingHeader = needsSteamCmdFetch ? '<div id="sls-resolving-pill" class="sls-resolving-pill"><div class="sls-mini-spinner"></div><span>Resolving DLC details…</span></div>' : '';
            contentDiv.innerHTML = resolvingHeader + html;

            function dismissResolvingPill() {
                var pill = document.getElementById('sls-resolving-pill');
                if (pill) {
                    pill.style.opacity = '0';
                    setTimeout(function() { if (pill && pill.parentNode) pill.remove(); }, 300);
                }
            }

            // Async fetch from SteamCMD for DLC names, unknown sizes, and missing OS info
            var depotItems = contentDiv.querySelectorAll('.sls-depot-item');
            if (needsSteamCmdFetch) {
                fetch('https://api.steamcmd.net/v1/info/' + appid)
                    .then(function(r) { return r.json(); })
                    .then(function(steamData) {
                        dismissResolvingPill();
                        if (!steamData || !steamData.data || !steamData.data[appid]) return;
                        var appData = steamData.data[appid];
                        
                        for (var m = 0; m < depotItems.length; m++) {
                            var el = depotItems[m];
                            var nameEl = el.querySelector('.sls-depot-name');
                            var metaEl = el.querySelector('.sls-depot-meta');
                            var sizeSpan = metaEl.querySelectorAll('span')[1];
                            var cb = el.querySelector('.sls-depot-checkbox');
                            var did = cb.value;
                            var hasOs = cb.getAttribute('data-os');
                            
                            // Resolve DLC names
                            if (nameEl.innerText.indexOf('Additional Content') === 0) {
                                var depotMeta = appData.depots && appData.depots[did];
                                if (depotMeta && depotMeta.dlcappid) {
                                    // Fetch the DLC app name
                                    (function(nEl, dlcId) {
                                        fetch('https://api.steamcmd.net/v1/info/' + dlcId)
                                            .then(function(r2) { return r2.json(); })
                                            .then(function(dlcData) {
                                                if (dlcData && dlcData.data && dlcData.data[dlcId] && dlcData.data[dlcId].common && dlcData.data[dlcId].common.name) {
                                                    // Preserve any OS badge already appended
                                                    var existingBadge = nEl.querySelector('.sls-os-badge');
                                                    nEl.innerText = dlcData.data[dlcId].common.name;
                                                    if (existingBadge) nEl.appendChild(existingBadge);
                                                }
                                            }).catch(function(){});
                                    })(nameEl, depotMeta.dlcappid);
                                }
                            }
                            
                            // Resolve unknown sizes
                            if (sizeSpan.innerText === 'Unknown size' && appData.depots && appData.depots[did] && appData.depots[did].manifests && appData.depots[did].manifests.public && appData.depots[did].manifests.public.size) {
                                sizeSpan.innerText = formatBytes(parseInt(appData.depots[did].manifests.public.size, 10));
                            }
                            
                            // Resolve missing OS info from SteamCMD
                            if (!hasOs && appData.depots && appData.depots[did] && appData.depots[did].config && appData.depots[did].config.oslist) {
                                var oslist = appData.depots[did].config.oslist;
                                cb.setAttribute('data-os', oslist);
                                var badge = buildOsBadgeElement(oslist);
                                if (badge) nameEl.appendChild(badge);
                                
                                // If this was labeled Additional Content but it's actually a base game OS depot, fix the name
                                if (nameEl.innerText.indexOf('Additional Content') === 0) {
                                    var depotIdNum = parseInt(did, 10);
                                    var appIdNum = parseInt(appid, 10);
                                    if (depotIdNum >= appIdNum && depotIdNum <= appIdNum + 10) {
                                        nameEl.innerText = 'Base Game Content';
                                        nameEl.appendChild(badge);
                                    }
                                }
                            }
                        }
                    }).catch(function() {
                        dismissResolvingPill();
                    });
            }

            // Add event listeners to checkboxes to enable/disable the download button
            var checkboxes = contentDiv.querySelectorAll('.sls-depot-checkbox');
            for (var j = 0; j < checkboxes.length; j++) {
                checkboxes[j].addEventListener('change', function() {
                    var anyChecked = false;
                    for (var k = 0; k < checkboxes.length; k++) {
                        if (checkboxes[k].checked) {
                            anyChecked = true;
                            break;
                        }
                    }
                    if (anyChecked) {
                        downloadBtn.style.opacity = '1';
                        downloadBtn.style.pointerEvents = 'auto';
                    } else {
                        downloadBtn.style.opacity = '0.5';
                        downloadBtn.style.pointerEvents = 'none';
                    }
                });
            }

            // Controller support: collect all interactive elements
            var trapButtons = [
                [closeX]
            ];
            for (var t = 0; t < checkboxes.length; t++) {
                // Use the parent label so the outline wraps the whole row
                var label = checkboxes[t].closest ? checkboxes[t].closest('.sls-depot-item') : checkboxes[t].parentElement;
                trapButtons.push([label || checkboxes[t]]);
            }
            trapButtons.push([cancelBtn, downloadBtn]);
            setupGamepadTrap(trapButtons, 1, 0); // Focus first depot row!
            
            downloadBtn.onclick = function() {
                var selected = [];
                for (var k = 0; k < checkboxes.length; k++) {
                    if (checkboxes[k].checked) {
                        selected.push(checkboxes[k].value);
                    }
                }
                
                if (selected.length === 0) return;
                
                downloadBtn.innerText = 'Starting...';
                downloadBtn.style.opacity = '0.5';
                downloadBtn.style.pointerEvents = 'none';
                cancelBtn.style.pointerEvents = 'none';
                closeX.style.pointerEvents = 'none';
                
                var depotsStr = encodeURIComponent(selected.join(','));
                fetch('http://127.0.0.1:9001/start-download?id=' + appid + '&depots=' + depotsStr, { mode: 'no-cors' })
                    .then(function() {
                        try {
                            var evt = new CustomEvent('sls-download-started', { detail: { appid: appid } });
                            window.dispatchEvent(evt);
                            window.postMessage({ type: 'sls-download-started', appid: appid }, '*');
                            if (window.top && window.top !== window) {
                                window.top.dispatchEvent(new CustomEvent('sls-download-started', { detail: { appid: appid } }));
                                window.top.postMessage({ type: 'sls-download-started', appid: appid }, '*');
                            }
                        } catch(e) {}
                        closeSuccess();
                    })
                    .catch(function() {
                        downloadBtn.innerText = 'Failed';
                        setTimeout(function() {
                            downloadBtn.innerText = 'Download';
                            downloadBtn.style.opacity = '1';
                            downloadBtn.style.pointerEvents = 'auto';
                            cancelBtn.style.pointerEvents = 'auto';
                            closeX.style.pointerEvents = 'auto';
                        }, 2000);
                    });
            };
        })
        .catch(function(err) {
            contentDiv.innerHTML = '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#ef4444; font-size: 14px;">Failed to fetch depot list.</div>';
            setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);
        });

})();
