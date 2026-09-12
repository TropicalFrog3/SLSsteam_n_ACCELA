(function() {
    'use strict';
    
    // Prevent double injection if somehow triggered twice rapidly
    if (document.getElementById('sls-depot-overlay-modal')) return;

    var appid = "%APPID%";

    var overlay = document.createElement('div');
    overlay.id = 'sls-depot-overlay-modal';
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

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
            '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#8a8d96; font-size: 14px;">Loading depots...</div>' +
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
        ' #sls-depot-content::-webkit-scrollbar-thumb:hover { background: rgba(255,255,255,0.2); }';
    overlay.appendChild(style);
    document.body.appendChild(overlay);

    requestAnimationFrame(function() {
        overlay.style.opacity = '1';
        var card = document.getElementById('sls-depot-modal-card');
        if (card) {
            card.style.transform = 'scale(1)';
            card.style.opacity = '1';
        }
    });

    var closeX = document.getElementById('sls-depot-close-x');
    var cancelBtn = document.getElementById('sls-depot-btn-cancel');
    var downloadBtn = document.getElementById('sls-depot-btn-download');
    var contentDiv = document.getElementById('sls-depot-content');

    function formatBytes(bytes, decimals) {
        if(bytes == 0) return '0 Bytes';
        if(decimals === undefined) decimals = 2;
        var k = 1024,
            dm = decimals < 0 ? 0 : decimals,
            sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'],
            i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
    }

    function closeAndCancel() {
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
                return;
            }

            if (!data.depots || data.depots.length === 0) {
                contentDiv.innerHTML = '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#f59e0b; font-size: 14px;">No depots found in Lua plugin.</div>';
                return;
            }

            var html = '';
            for (var i = 0; i < data.depots.length; i++) {
                var depot = data.depots[i];
                var sizeStr = depot.size ? formatBytes(parseInt(depot.size, 10)) : 'Unknown size';
                
                html += '<label class="sls-depot-item">' +
                    '<input type="checkbox" class="sls-depot-checkbox" value="' + depot.id + '">' +
                    '<div class="sls-depot-info">' +
                        '<div class="sls-depot-name">' + depot.name + '</div>' +
                        '<div class="sls-depot-meta">' +
                            '<span>ID: ' + depot.id + '</span>' +
                            '<span>' + sizeStr + '</span>' +
                        '</div>' +
                    '</div>' +
                '</label>';
            }
            
            contentDiv.innerHTML = html;

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
        });

})();
