(function () {
    'use strict';
    if (window.__slsImportLuaInjected) return;
    window.__slsImportLuaInjected = true;

    function log(m) {
        console.log('[SLS] ' + m);
        fetch('http://127.0.0.1:9001/log?msg=' + encodeURIComponent('[RemoveLua] ' + m)).catch(function () { });
    }

    // Dynamically load JSZip library for client-side zip extraction
    var _jszipPromise = null;
    function loadJSZip() {
        if (typeof JSZip !== 'undefined') return Promise.resolve(JSZip);
        if (_jszipPromise) return _jszipPromise;
        _jszipPromise = new Promise(function (resolve, reject) {
            var script = document.createElement('script');
            script.src = 'https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js';
            script.onload = function () {
                if (typeof JSZip !== 'undefined') {
                    log('JSZip loaded successfully');
                    resolve(JSZip);
                } else {
                    reject(new Error('JSZip not available after script load'));
                }
            };
            script.onerror = function () {
                _jszipPromise = null;
                reject(new Error('Failed to load JSZip from CDN'));
            };
            document.head.appendChild(script);
        });
        return _jszipPromise;
    }

    // Known Steam CSS class names (hashed, from CEF inspection)
    var STEAM_NAV_CONTAINER_CLASS = '_2D64jIEK7wpUR_NlObDW76';
    var STEAM_TAB_BASE_CLASS = '_2Lu3d-5qLmW4i19ysTt2jT';
    var STEAM_TAB_ACTIVE_CLASS = '_1gqEjB5QsKT_NftD1dEsdZ';
    var STEAM_TAB_TEXT_CLASS = '_19axKcqYRuaJ8vdYKYmtTQ';

    function findSuperNavContainer() {
        // Primary: use the known hashed class
        var container = document.querySelector('.' + STEAM_NAV_CONTAINER_CLASS);
        if (container) return container;

        // Fallback: search for an element whose direct DIV children contain Store/Library/Community text
        var divs = document.querySelectorAll('div');
        for (var i = 0; i < divs.length; i++) {
            var el = divs[i];
            if (el.children.length >= 4 && el.children.length <= 12) {
                var text = (el.textContent || '').toUpperCase();
                if (text.indexOf('STORE') !== -1 && text.indexOf('LIBRARY') !== -1 && text.indexOf('COMMUNITY') !== -1) {
                    var rect = el.getBoundingClientRect();
                    if (rect.top >= 0 && rect.top < 150 && rect.height > 15 && rect.height < 100) {
                        return el;
                    }
                }
            }
        }
        return null;
    }

    function getTopBarHeight() {
        // Use the outer wrapper of the nav bar (_3Z3ohQ8-1NKnCZkbS6fvy) 
        var navContainer = findSuperNavContainer();
        if (navContainer) {
            // Walk up to find the full top bar area
            var el = navContainer;
            while (el && el !== document.body) {
                var rect = el.getBoundingClientRect();
                if (rect.top === 0 && rect.height > 30 && rect.height < 150) {
                    return rect.height;
                }
                el = el.parentElement;
            }
            return navContainer.getBoundingClientRect().bottom;
        }
        return 66;
    }

    function setActiveTabStyle(activeTab) {
        var container = findSuperNavContainer();
        if (!container) return;

        // Remove active class from all sibling tabs
        var tabs = container.querySelectorAll('.' + STEAM_TAB_BASE_CLASS);
        for (var i = 0; i < tabs.length; i++) {
            tabs[i].classList.remove(STEAM_TAB_ACTIVE_CLASS);
        }

        // Add active class to the target tab
        if (activeTab) {
            activeTab.classList.add(STEAM_TAB_ACTIVE_CLASS);
        }
    }

    function getSteamHistory() {
        // Extract React Router history from the fiber tree of a tab element
        var container = findSuperNavContainer();
        if (!container) return null;
        var tabEl = container.querySelector('.' + STEAM_TAB_BASE_CLASS);
        if (!tabEl) return null;
        var reactFiberKey = null;
        for (var key in tabEl) {
            if (key.indexOf('__reactFiber') === 0 || key.indexOf('__reactInternalInstance') === 0) {
                reactFiberKey = key;
                break;
            }
        }
        if (!reactFiberKey) return null;
        var fiber = tabEl[reactFiberKey];
        while (fiber) {
            if (fiber.memoizedProps && fiber.memoizedProps.history) {
                return fiber.memoizedProps.history;
            }
            fiber = fiber.return;
        }
        return null;
    }

    var historyUnlisten = null;

    var activeSlsAppId = null;

    function showSlsPage() {
        // Use Steam's React Router to navigate to /library/home which hides the browser overlay
        var history = getSteamHistory();
        if (history) {
            window.__slsNavigating = true;
            history.push('/library/home');
        }

        var container = document.getElementById('sls-page-container');
        if (!container) {
            // Inject CSS override to suppress other tabs' active styles when SLS tab is active
            var styleEl = document.createElement('style');
            styleEl.innerHTML =
                'body:has(#sls-top-tab.' + STEAM_TAB_ACTIVE_CLASS + ') .' + STEAM_TAB_BASE_CLASS + ':not(#sls-top-tab) .' + STEAM_TAB_TEXT_CLASS + ' {' +
                '    color: rgb(150, 150, 150) !important;' +
                '    text-shadow: none !important;' +
                '    font-weight: normal !important;' +
                '}' +
                'body:has(#sls-top-tab.' + STEAM_TAB_ACTIVE_CLASS + ') .' + STEAM_TAB_BASE_CLASS + ':not(#sls-top-tab) .' + STEAM_TAB_TEXT_CLASS + '::after {' +
                '    display: none !important;' +
                '}' +
                'details summary::-webkit-details-marker { display: none !important; }' +
                'details summary { list-style: none !important; }' +
                'details .sls-lua-chevron { transition: transform 0.2s ease; color: #8f98a0; }' +
                'details[open] .sls-lua-chevron { transform: rotate(180deg); color: #66c0f4; }';
            document.head.appendChild(styleEl);

            container = document.createElement('div');
            container.id = 'sls-page-container';
            container.style.cssText = 'position: fixed; left: 0; right: 0; bottom: 0; z-index: 999999; background: radial-gradient(circle at top, #1b2838 0%, #0d121a 100%); display: none; overflow-y: auto; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; color: #d6d7d9; padding: 40px 60px;';
            document.body.appendChild(container);
        }

        container.innerHTML = '<div style="max-width: 1000px; margin: 0 auto;">' +
            '<div style="margin-bottom: 40px; display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid rgba(255,255,255,0.06); padding-bottom: 24px;">' +
            '<div>' +
            '<h1 style="margin: 0; font-size: 32px; font-weight: 800; background: linear-gradient(90deg, #66c0f4 0%, #1a9fff 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent; letter-spacing: -0.5px;">Manual Installation</h1>' +
            '<p style="margin: 8px 0 0; font-size: 14px; color: #8f98a0;">Manually install Lua plugins and Manifest depotcaches for any Steam game.</p>' +
            '</div>' +
            '</div>' +
            '<div style="display: flex; flex-direction: column; gap: 0;">' +
            // Single Column: Upload Zone, File List, AppID, and Install
            '<div style="width: 100%; box-sizing: border-box; background: rgba(255, 255, 255, 0.02); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 16px; padding: 32px; display: flex; flex-direction: column; overflow: hidden;">' +
            '<div id="sls-manual-dropzone" style="min-height: 180px; border: 2px dashed rgba(255, 255, 255, 0.15); border-radius: 12px; background: rgba(0,0,0,0.15); display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 30px; text-align: center; cursor: pointer; transition: all 0.2s;">' +
            '<svg style="width: 48px; height: 48px; color: #66c0f4; margin-bottom: 16px;" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">' +
            '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M15 13l-3-3m0 0l-3 3m3-3v12"></path>' +
            '</svg>' +
            '<p style="margin: 0; font-size: 15px; font-weight: 600; color: #fff;">Supports .zip, .lua, .manifest, or entire folders</p>' +
            '<div style="margin-top: 20px; display: flex; gap: 10px;">' +
            '<button id="sls-manual-select-files" style="background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; color: #fff; padding: 8px 16px; font-size: 13px; font-weight: 600; cursor: pointer; transition: background 0.2s; outline: none;">' +
            'Select Files' +
            '</button>' +
            '<button id="sls-manual-select-folder" style="background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; color: #fff; padding: 8px 16px; font-size: 13px; font-weight: 600; cursor: pointer; transition: background 0.2s; outline: none;">' +
            'Select Folder' +
            '</button>' +
            '</div>' +
            '</div>' +
            '<div id="sls-manual-file-list" style="max-height: 480px; overflow-y: auto; display: none; flex-direction: column; gap: 8px; border-bottom: 1px solid rgba(255,255,255,0.06); padding-bottom: 16px; margin-top: 24px;">' +
            '</div>' +
            '<div style="margin-top: 24px;">' +
            '<div id="sls-manual-status" style="margin-bottom: 12px; font-size: 14px; text-align: center; color: #8f98a0; font-weight: 600;">' +
            '</div>' +
            '<button id="sls-manual-submit" disabled style="width: 100%; background: rgba(255, 255, 255, 0.05); border: none; border-radius: 8px; color: rgba(255,255,255,0.3); padding: 16px; font-size: 15px; font-weight: 700; cursor: not-allowed; outline: none;">' +
            'Import' +
            '</button>' +
            '</div>' +
            '</div>' +
            '</div>' +
            '</div>';

        initManualInstallPage();

        container.style.top = getTopBarHeight() + 'px';
        container.style.display = 'block';

        // Register history listener to auto-hide when user navigates away
        if (!historyUnlisten) {
            var history = getSteamHistory();
            if (history) {
                historyUnlisten = history.listen(function (location) {
                    if (window.__slsNavigating) {
                        window.__slsNavigating = false;
                        return;
                    }
                    hideSlsPage();
                });
            }
        }

        var slsTab = document.getElementById('sls-top-tab');
        if (slsTab) {
            setActiveTabStyle(slsTab);
        }
    }

    function hideSlsPage() {
        var container = document.getElementById('sls-page-container');
        if (container) {
            container.style.display = 'none';
        }
        var slsTab = document.getElementById('sls-top-tab');
        if (slsTab) {
            slsTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);
        }
    }

    function initManualInstallPage() {
        var dropzone = document.getElementById('sls-manual-dropzone');
        var fileList = document.getElementById('sls-manual-file-list');
        var status = document.getElementById('sls-manual-status');
        var submitBtn = document.getElementById('sls-manual-submit');
        var selectFilesBtn = document.getElementById('sls-manual-select-files');
        var selectFolderBtn = document.getElementById('sls-manual-select-folder');

        var selectedFiles = [];
        var activeAppId = null;
        var activeAppMetadata = null;
        var fetchedMetadataCache = {};
        var checkedFiles = {};
        var openDetails = {};
        var luaContentsCache = {};

        // 1. More robust attribute setting for Steam CEF
        var fileInput = document.createElement('input');
        fileInput.type = 'file';
        
        // Brute-force the multiple flag for CEF
        fileInput.multiple = true; 
        fileInput.setAttribute('multiple', 'true');
        fileInput.setAttribute('multiple', 'multiple');
        
        // This is the magic bullet for some CEF versions. 
        // Forcing an 'accept' type often triggers the newer, multi-select capable Windows dialog.
        fileInput.setAttribute('accept', '*/*'); 
        
        fileInput.style.display = 'none';
        document.body.appendChild(fileInput);

        var folderInput = document.createElement('input');
        folderInput.type = 'file';
        folderInput.setAttribute('webkitdirectory', '');
        folderInput.setAttribute('directory', '');
        folderInput.style.display = 'none';
        document.body.appendChild(folderInput);

        selectFilesBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            fileInput.click();
        };

        selectFolderBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            folderInput.click();
        };

        function loadLuaFilesCache(files, callback) {
            var luaFilesToRead = files.filter(function (file) {
                return file.name.toLowerCase().endsWith('.lua') && typeof luaContentsCache[file.name] === 'undefined';
            });

            if (luaFilesToRead.length === 0) {
                callback();
                return;
            }

            var remaining = luaFilesToRead.length;
            luaFilesToRead.forEach(function (file) {
                var reader = new FileReader();
                reader.onload = function (e) {
                    luaContentsCache[file.name] = e.target.result;
                    remaining--;
                    if (remaining === 0) {
                        callback();
                    }
                };
                reader.onerror = function () {
                    luaContentsCache[file.name] = '';
                    remaining--;
                    if (remaining === 0) {
                        callback();
                    }
                };
                reader.readAsText(file);
            });
        }

        // Extract zip files into individual File objects
        function extractZipFiles(zipFile) {
            return loadJSZip().then(function (JSZipLib) {
                return JSZipLib.loadAsync(zipFile).then(function (zip) {
                    var promises = [];
                    zip.forEach(function (relativePath, zipEntry) {
                        if (zipEntry.dir) return;
                        // Only extract relevant file types
                        var lowerName = zipEntry.name.toLowerCase();
                        if (!lowerName.endsWith('.lua') && !lowerName.endsWith('.manifest')) return;
                        promises.push(
                            zipEntry.async('blob').then(function (blob) {
                                // Use just the filename (strip directory paths inside zip)
                                var fileName = zipEntry.name.split('/').pop();
                                return new File([blob], fileName, { type: blob.type || 'application/octet-stream' });
                            })
                        );
                    });
                    return Promise.all(promises);
                });
            });
        }

        function handleFiles(files) {
            var newFiles = Array.prototype.slice.call(files);

            // Separate zip files from regular files, filtering regular files by extension (.lua and .manifest only)
            var regularFiles = [];
            var zipFiles = [];
            newFiles.forEach(function (file) {
                var lowerName = file.name.toLowerCase();
                if (lowerName.endsWith('.zip')) {
                    zipFiles.push(file);
                } else if (lowerName.endsWith('.lua') || lowerName.endsWith('.manifest')) {
                    regularFiles.push(file);
                }
            });

            // Process regular files immediately
            function addFiles(filesToAdd) {
                filesToAdd.forEach(function (newFile) {
                    var exists = selectedFiles.some(function (existingFile) {
                        return existingFile.name === newFile.name;
                    });
                    if (!exists) {
                        selectedFiles.push(newFile);
                    }
                });
            }

            if (zipFiles.length === 0) {
                // No zips, process normally
                addFiles(regularFiles);
                loadLuaFilesCache(regularFiles, function () {
                    initCheckedFiles();
                    updateFileList();
                });
                return;
            }

            // Extract all zip files, then merge with regular files
            var zipPromises = zipFiles.map(function (zf) {
                return extractZipFiles(zf).catch(function (err) {
                    log('Failed to extract ' + zf.name + ': ' + err.message);
                    // If extraction fails, don't show the zip file in the list since it's not a .lua/.manifest
                    return [];
                });
            });

            Promise.all(zipPromises).then(function (results) {
                var allExtracted = [];
                results.forEach(function (extractedFiles) {
                    allExtracted = allExtracted.concat(extractedFiles);
                });
                var combined = regularFiles.concat(allExtracted);
                addFiles(combined);
                loadLuaFilesCache(combined, function () {
                    initCheckedFiles();
                    updateFileList();
                });
            });
        }

        // 3. Clear the input value so onchange fires even if the same file is selected again
        fileInput.onchange = function () {
            handleFiles(fileInput.files);
            fileInput.value = '';
        };
        folderInput.onchange = function () {
            handleFiles(folderInput.files);
            folderInput.value = '';
        };

        dropzone.ondragover = function (e) {
            e.preventDefault(); e.stopPropagation();
            dropzone.style.borderColor = '#1a9fff';
            dropzone.style.background = 'rgba(26, 159, 255, 0.05)';
        };

        dropzone.ondragleave = function (e) {
            e.preventDefault(); e.stopPropagation();
            dropzone.style.borderColor = 'rgba(255, 255, 255, 0.15)';
            dropzone.style.background = 'rgba(0,0,0,0.15)';
        };

        dropzone.ondrop = function (e) {
            e.preventDefault(); e.stopPropagation();
            dropzone.style.borderColor = 'rgba(255, 255, 255, 0.15)';
            dropzone.style.background = 'rgba(0,0,0,0.15)';
            if (e.dataTransfer && e.dataTransfer.files) {
                handleFiles(e.dataTransfer.files);
            }
        };

        dropzone.onclick = function (e) {
            if (e.target !== selectFilesBtn && e.target !== selectFolderBtn) {
                fileInput.click();
            }
        };

        var pendingFetches = {};

        function fetchAppMetadata(appid) {
            if (!appid) return;
            if (fetchedMetadataCache[appid]) {
                if (activeAppId === appid) {
                    activeAppMetadata = fetchedMetadataCache[appid];
                }
                updateFileList();
                return;
            }
            if (pendingFetches[appid]) return;
            pendingFetches[appid] = true;

            fetch('https://api.steamcmd.net/v1/info/' + appid)
                .then(function (r) { return r.json(); })
                .then(function (data) {
                    var appData = data && data.data && data.data[appid];
                    if (appData) {
                        fetchedMetadataCache[appid] = appData;
                        updateFileList();
                    }
                    delete pendingFetches[appid];
                })
                .catch(function (err) {
                    console.warn('[SLS] Failed to fetch metadata for:', appid, err);
                    delete pendingFetches[appid];
                });
        }

        function getAppIdFromFileName(fileName) {
            var manifestMatch = fileName.match(/^([0-9]+)_[0-9]+\.manifest$/);
            if (manifestMatch) return manifestMatch[1];

            var dotIndex = fileName.lastIndexOf('.');
            var baseName = dotIndex !== -1 ? fileName.substring(0, dotIndex) : fileName;
            if (/^[0-9]+$/.test(baseName)) return baseName;

            var match = fileName.match(/([0-9]+)/);
            if (match) return match[1];

            return null;
        }

        function classifyFile(file, parentAppId) {
            if (file.name.toLowerCase().endsWith('.lua')) return 'lua';

            var manifestMatch = file.name.toLowerCase().match(/^([0-9]+)_[0-9]+\.manifest$/);
            if (manifestMatch) {
                var depotId = manifestMatch[1];
                var appMetadata = null;
                if (parentAppId && fetchedMetadataCache[parentAppId]) {
                    appMetadata = fetchedMetadataCache[parentAppId];
                } else if (activeAppMetadata) {
                    appMetadata = activeAppMetadata;
                } else {
                    // Try to find the depot across all cached app metadata
                    for (var appid in fetchedMetadataCache) {
                        var meta = fetchedMetadataCache[appid];
                        if (meta && meta.depots && meta.depots[depotId]) {
                            appMetadata = meta;
                            break;
                        }
                    }
                }

                if (appMetadata && appMetadata.depots && appMetadata.depots[depotId]) {
                    var depot = appMetadata.depots[depotId];
                    var osList = depot.config && depot.config.oslist || '';

                    if (!osList) {
                        var launchObj = appMetadata.config && appMetadata.config.launch;
                        if (launchObj) {
                            var firstLaunch = launchObj['0'] || (Array.isArray(launchObj) ? launchObj[0] : null);
                            if (firstLaunch && firstLaunch.config && firstLaunch.config.oslist) {
                                osList = firstLaunch.config.oslist;
                            } else {
                                var launchEntries = Array.isArray(launchObj) ? launchObj : Object.values(launchObj);
                                for (var li = 0; li < launchEntries.length; li++) {
                                    var entry = launchEntries[li];
                                    if (entry && entry.config && entry.config.oslist) {
                                        osList = entry.config.oslist;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (depot.dlcappid || (depot.name && depot.name.toLowerCase().indexOf('dlc') !== -1)) {
                        return 'dlc';
                    } else if (osList.indexOf('windows') !== -1) {
                        return 'windows';
                    } else if (osList.indexOf('linux') !== -1) {
                        return 'linux';
                    } else if (osList.indexOf('macos') !== -1 || osList.indexOf('osx') !== -1) {
                        return 'macos';
                    }
                }
                return 'other';
            }

            return 'other';
        }

        function initCheckedFiles() {
            // Don't reset the object entirely. Only default new files to true.
            selectedFiles.forEach(function (file) {
                if (typeof checkedFiles[file.name] === 'undefined') {
                    checkedFiles[file.name] = true;
                }
            });
        }

        function getCheckedOSCount() {
            var count = 0;
            selectedFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                if ((cat === 'windows' || cat === 'linux' || cat === 'macos') && checkedFiles[file.name]) {
                    count++;
                }
            });
            return count;
        }

        function findRelatedLuaFile(file, luaFiles) {
            if (luaFiles.length === 0) return null;
            if (luaFiles.length === 1) return luaFiles[0];

            var isManifest = file.name.toLowerCase().endsWith('.manifest');
            if (isManifest) {
                // Extract numbers from the manifest file name (e.g. depot ID and manifest ID)
                var numbers = file.name.match(/[0-9]+/g) || [];

                for (var i = 0; i < luaFiles.length; i++) {
                    var luaName = luaFiles[i].name;
                    var luaContent = luaContentsCache[luaName];
                    if (luaContent) {
                        for (var j = 0; j < numbers.length; j++) {
                            var num = numbers[j];
                            if (num.length >= 5) { // Only match significant numbers
                                if (luaContent.indexOf(num) !== -1) {
                                    return luaFiles[i];
                                }
                            }
                        }
                    }
                }
            }

            var fileAppId = getAppIdFromFileName(file.name);
            if (fileAppId) {
                for (var i = 0; i < luaFiles.length; i++) {
                    var luaAppId = getAppIdFromFileName(luaFiles[i].name);
                    if (luaAppId === fileAppId) {
                        return luaFiles[i];
                    }
                }
            }
            return luaFiles[0];
        }

        // Extract AppID from lua file contents by parsing addappid(XXXX) calls
        function getAppIdFromLuaContent(luaContent) {
            if (!luaContent) return null;
            var match = luaContent.match(/addappid\s*\(\s*([0-9]+)\s*\)/);
            return match ? match[1] : null;
        }

        // Find the best AppID from all checked lua files
        function detectAppIdFromCheckedFiles() {
            for (var i = 0; i < selectedFiles.length; i++) {
                var file = selectedFiles[i];
                if (file.name.toLowerCase().endsWith('.lua') && checkedFiles[file.name] !== false) {
                    var content = luaContentsCache[file.name];
                    var appid = getAppIdFromLuaContent(content);
                    if (appid) return appid;
                    // Fallback: try extracting from filename
                    var fromName = getAppIdFromFileName(file.name);
                    if (fromName) return fromName;
                }
            }
            // Fallback: try manifest filenames
            for (var j = 0; j < selectedFiles.length; j++) {
                var mf = selectedFiles[j];
                if (mf.name.toLowerCase().endsWith('.manifest') && checkedFiles[mf.name] !== false) {
                    var mfAppId = getAppIdFromFileName(mf.name);
                    if (mfAppId) return mfAppId;
                }
            }
            return null;
        }

        // Update the submit button state based on current checked files
        function updateSubmitButton() {
            var hasCheckedFiles = selectedFiles.some(function (f) {
                return checkedFiles[f.name] !== false;
            });
            var detectedAppId = detectAppIdFromCheckedFiles();
            activeAppId = detectedAppId;

            if (hasCheckedFiles && detectedAppId) {
                submitBtn.disabled = false;
                submitBtn.style.background = 'linear-gradient(90deg, #1a9fff 0%, #66c0f4 100%)';
                submitBtn.style.color = '#fff';
                submitBtn.style.cursor = 'pointer';
            } else {
                submitBtn.disabled = true;
                submitBtn.style.background = 'rgba(255, 255, 255, 0.05)';
                submitBtn.style.color = 'rgba(255,255,255,0.3)';
                submitBtn.style.cursor = 'not-allowed';
            }
        }

        function updateFileList() {
            fileList.innerHTML = '';
            if (selectedFiles.length === 0) {
                fileList.style.display = 'none';
                updateSubmitButton();
                return;
            }
            fileList.style.display = 'flex';

            var badgeStyles = {
                lua: { label: 'LUA', color: 'rgba(156, 39, 176, 0.15)', textColor: '#ce93d8' },
                windows: { label: 'Windows', color: 'rgba(26, 159, 255, 0.15)', textColor: '#66c0f4' },
                linux: { label: 'Linux', color: 'rgba(76, 175, 80, 0.15)', textColor: '#4caf50' },
                macos: { label: 'macOS', color: 'rgba(200, 200, 200, 0.15)', textColor: '#e0e0e0' },
                dlc: { label: 'DLC', color: 'rgba(255, 193, 7, 0.15)', textColor: '#ffc107' },
                other: { label: 'Other', color: 'rgba(255, 255, 255, 0.08)', textColor: '#8f98a0' }
            };

            // 1. Separate files into Lua files and other files
            var luaFiles = selectedFiles.filter(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                return classifyFile(file, fileAppId) === 'lua';
            });

            var manifestRelations = {};
            luaFiles.forEach(function (lf) {
                manifestRelations[lf.name] = [];
            });

            var standaloneFiles = [];

            selectedFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                if (cat === 'lua') return;

                var isManifest = file.name.toLowerCase().endsWith('.manifest');
                if (isManifest && luaFiles.length > 0) {
                    var relatedLua = findRelatedLuaFile(file, luaFiles);
                    if (relatedLua) {
                        manifestRelations[relatedLua.name].push(file);
                        return;
                    }
                }
                standaloneFiles.push(file);
            });

            // 2. Render all Lua files first
            luaFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                var style = badgeStyles[cat];

                var fileDisplayName = 'Unknown';
                var fileAppId = getAppIdFromFileName(file.name);

                if (fileAppId) {
                    if (fetchedMetadataCache[fileAppId]) {
                        var appData = fetchedMetadataCache[fileAppId];
                        var name = appData.common && appData.common.name || fileAppId;
                        var parentName = activeAppMetadata && activeAppMetadata.common && activeAppMetadata.common.name;
                        if (parentName && name.indexOf(parentName) === 0) {
                            var sub = name.substring(parentName.length).trim();
                            sub = sub.replace(/^[:\-\s]+/, '').replace(/^DLC\s*[:\-\s]*/i, '').trim();
                            if (sub) name = sub;
                        }
                        fileDisplayName = name;
                    } else {
                        fetchAppMetadata(fileAppId);
                        fileDisplayName = 'AppID ' + fileAppId;
                    }
                } else {
                    fileDisplayName = file.name;
                }

                var item = document.createElement('details');
                item.style.cssText = 'background:rgba(255,255,255,0.03); border:1px solid rgba(255,255,255,0.05); border-radius:6px; font-size:14px; overflow:hidden; transition:opacity 0.15s, background 0.15s; flex-shrink:0; width:100%; box-sizing:border-box;';

                if (openDetails[file.name]) {
                    item.setAttribute('open', '');
                }

                item.ontoggle = function () {
                    openDetails[file.name] = item.open;
                    
                    // Force a reflow on parent containers to fix Chromium CEF flex scroll height bugs
                    var originalOverflow = fileList.style.overflowY;
                    fileList.style.overflowY = 'hidden';
                    fileList.offsetHeight; // trigger reflow
                    fileList.style.overflowY = originalOverflow;

                    var pageContainer = document.getElementById('sls-page-container');
                    if (pageContainer) {
                        var pageOverflow = pageContainer.style.overflowY;
                        pageContainer.style.overflowY = 'hidden';
                        pageContainer.offsetHeight; // trigger reflow
                        pageContainer.style.overflowY = pageOverflow;
                    }
                };

                var summary = document.createElement('summary');
                summary.style.cssText = 'display:flex; justify-content:space-between; align-items:center; padding:20px 24px; cursor:pointer; user-select:none; outline:none; list-style:none; gap:12px;';

                var leftSide = document.createElement('div');
                leftSide.style.cssText = 'display:flex; align-items:center; gap:8px; overflow:hidden; flex:1; min-width:0;';

                var badge = document.createElement('span');
                badge.innerText = style.label;
                badge.style.cssText = 'background:' + style.color + '; color:' + style.textColor + '; border:1px solid ' + style.color.replace('0.15', '0.3').replace('0.08', '0.2') + '; font-size:10px; font-weight:700; text-transform:uppercase; padding:2px 6px; border-radius:4px; letter-spacing:0.5px; white-space:nowrap; flex-shrink:0;';
                leftSide.appendChild(badge);

                var nameSpan = document.createElement('span');
                nameSpan.innerText = fileDisplayName;
                nameSpan.style.cssText = 'color:#fff; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex-shrink:1; min-width:0;';
                leftSide.appendChild(nameSpan);

                summary.appendChild(leftSide);

                var rightSide = document.createElement('div');
                rightSide.style.cssText = 'display:flex; align-items:center; gap:12px; flex-shrink:0;';

                var sizeSpan = document.createElement('span');
                var sizeKB = Math.round(file.size / 1024 * 10) / 10;
                var sizeStr = sizeKB >= 1024 ? (Math.round(sizeKB / 1024 * 10) / 10) + ' MB' : sizeKB + ' KB';
                sizeSpan.innerText = sizeStr;
                sizeSpan.style.cssText = 'color:#8f98a0; white-space:nowrap; text-align:right; font-family:monospace; min-width:85px;';
                rightSide.appendChild(sizeSpan);

                var chevron = document.createElement('div');
                chevron.className = 'sls-lua-chevron';
                chevron.innerHTML = '<svg style="width: 16px; height: 16px; display: block;" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">' +
                    '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M19 9l-7 7-7-7"></path>' +
                    '</svg>';
                rightSide.appendChild(chevron);

                summary.appendChild(rightSide);
                item.appendChild(summary);

                var contentContainer = document.createElement('div');
                contentContainer.style.cssText = 'padding: 0 24px 20px 24px; border-top: 1px solid rgba(255,255,255,0.03); background: rgba(0,0,0,0.1); display: flex; flex-direction: column; gap: 20px;';

                // Render related manifest files inside details
                var relatedManifests = manifestRelations[file.name] || [];
                if (relatedManifests.length > 0) {
                    // Sort relatedManifests by category: linux, windows, macos, dlc, other
                    var categoryOrder = { linux: 1, windows: 2, macos: 3, dlc: 4, other: 5 };
                    relatedManifests.sort(function (a, b) {
                        var catA = classifyFile(a, fileAppId);
                        var catB = classifyFile(b, fileAppId);
                        var weightA = categoryOrder[catA] || 99;
                        var weightB = categoryOrder[catB] || 99;
                        if (weightA !== weightB) {
                            return weightA - weightB;
                        }
                        return a.name.localeCompare(b.name);
                    });

                    var manifestsSection = document.createElement('div');
                    manifestsSection.style.cssText = 'display: flex; flex-direction: column; gap: 8px; margin-top: 16px;';

                    var hasAnyUnchecked = relatedManifests.some(function (mFile) {
                        return checkedFiles[mFile.name] === false;
                    });

                    // Actions bar: Single toggle button
                    var actionsBar = document.createElement('div');
                    actionsBar.style.cssText = 'display: flex; gap: 12px; margin-bottom: 4px;';

                    var toggleBtn = document.createElement('button');
                    if (hasAnyUnchecked) {
                        toggleBtn.innerText = 'Check All';
                        toggleBtn.style.cssText = 'background: rgba(26, 159, 255, 0.1); border: 1px solid rgba(26, 159, 255, 0.2); border-radius: 4px; color: #66c0f4; padding: 4px 8px; font-size: 11px; font-weight: 700; cursor: pointer; text-transform: uppercase; letter-spacing: 0.5px; transition: all 0.2s; outline: none;';
                        toggleBtn.onmouseover = function () {
                            toggleBtn.style.background = 'rgba(26, 159, 255, 0.2)';
                            toggleBtn.style.borderColor = 'rgba(26, 159, 255, 0.4)';
                        };
                        toggleBtn.onmouseout = function () {
                            toggleBtn.style.background = 'rgba(26, 159, 255, 0.1)';
                            toggleBtn.style.borderColor = 'rgba(26, 159, 255, 0.2)';
                        };
                        toggleBtn.onclick = function (e) {
                            e.preventDefault(); e.stopPropagation();
                            relatedManifests.forEach(function (mFile) {
                                checkedFiles[mFile.name] = true;
                            });
                            updateFileList();
                        };
                    } else {
                        toggleBtn.innerText = 'Uncheck All';
                        toggleBtn.style.cssText = 'background: rgba(255, 255, 255, 0.05); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 4px; color: #8f98a0; padding: 4px 8px; font-size: 11px; font-weight: 700; cursor: pointer; text-transform: uppercase; letter-spacing: 0.5px; transition: all 0.2s; outline: none;';
                        toggleBtn.onmouseover = function () {
                            toggleBtn.style.background = 'rgba(255, 255, 255, 0.1)';
                            toggleBtn.style.borderColor = 'rgba(255, 255, 255, 0.2)';
                            toggleBtn.style.color = '#fff';
                        };
                        toggleBtn.onmouseout = function () {
                            toggleBtn.style.background = 'rgba(255, 255, 255, 0.05)';
                            toggleBtn.style.borderColor = 'rgba(255, 255, 255, 0.1)';
                            toggleBtn.style.color = '#8f98a0';
                        };
                        toggleBtn.onclick = function (e) {
                            e.preventDefault(); e.stopPropagation();
                            relatedManifests.forEach(function (mFile) {
                                checkedFiles[mFile.name] = false;
                            });
                            updateFileList();
                        };
                    }

                    actionsBar.appendChild(toggleBtn);
                    manifestsSection.appendChild(actionsBar);

                    var manifestsList = document.createElement('div');
                    manifestsList.style.cssText = 'display: flex; flex-direction: column; gap: 6px;';

                    relatedManifests.forEach(function (mFile) {
                        var mCat = classifyFile(mFile, fileAppId);
                        var mStyle = badgeStyles[mCat];
                        var mIsChecked = checkedFiles[mFile.name] !== false;

                        var mDisplayName = 'Unknown';
                        var mAppId = getAppIdFromFileName(mFile.name);

                        if (mAppId) {
                            if (fetchedMetadataCache[mAppId]) {
                                var appData = fetchedMetadataCache[mAppId];
                                var name = appData.common && appData.common.name || mAppId;
                                var parentName = activeAppMetadata && activeAppMetadata.common && activeAppMetadata.common.name;
                                if (parentName && name.indexOf(parentName) === 0) {
                                    var sub = name.substring(parentName.length).trim();
                                    sub = sub.replace(/^[:\-\s]+/, '').replace(/^DLC\s*[:\-\s]*/i, '').trim();
                                    if (sub) name = sub;
                                }
                                mDisplayName = name;
                            } else {
                                fetchAppMetadata(mAppId);
                                mDisplayName = 'AppID ' + mAppId;
                            }
                        } else {
                            mDisplayName = mFile.name;
                        }

                        var mItem = document.createElement('div');
                        mItem.style.cssText = 'display:flex; justify-content:space-between; align-items:center; background:rgba(255,255,255,0.02); border:1px solid rgba(255,255,255,0.04); padding:12px 16px; border-radius:6px; font-size:13px; gap:10px; overflow:hidden; cursor:pointer; user-select:none; transition:opacity 0.15s, background 0.15s;' + (!mIsChecked ? ' opacity:0.4;' : '');

                        var mCb = document.createElement('input');
                        mCb.type = 'checkbox';
                        mCb.checked = mIsChecked;
                        mCb.style.cssText = 'width:14px; height:14px; flex-shrink:0; pointer-events:none; accent-color:#1a9fff;';
                        mItem.appendChild(mCb);

                        (function (f) {
                            mItem.onclick = function (e) {
                                e.preventDefault(); e.stopPropagation();
                                checkedFiles[f.name] = checkedFiles[f.name] === false;
                                updateFileList();
                            };
                        })(mFile);

                        var mLeftSide = document.createElement('div');
                        mLeftSide.style.cssText = 'display:flex; align-items:center; gap:8px; overflow:hidden; flex:1; min-width:0;';

                        var mBadge = document.createElement('span');
                        mBadge.innerText = mStyle.label;
                        mBadge.style.cssText = 'background:' + mStyle.color + '; color:' + mStyle.textColor + '; border:1px solid ' + mStyle.color.replace('0.15', '0.3').replace('0.08', '0.2') + '; font-size:9px; font-weight:700; text-transform:uppercase; padding:1px 4px; border-radius:3px; letter-spacing:0.5px; white-space:nowrap; flex-shrink:0;';
                        mLeftSide.appendChild(mBadge);

                        var mNameSpan = document.createElement('span');
                        mNameSpan.innerText = mDisplayName;
                        mNameSpan.style.cssText = 'color:#e0e0e0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex-shrink:1; min-width:0;';
                        mLeftSide.appendChild(mNameSpan);

                        var mSizeSpan = document.createElement('span');
                        var mSizeKB = Math.round(mFile.size / 1024 * 10) / 10;
                        var mSizeStr = mSizeKB >= 1024 ? (Math.round(mSizeKB / 1024 * 10) / 10) + ' MB' : mSizeKB + ' KB';
                        mSizeSpan.innerText = mSizeStr;
                        mSizeSpan.style.cssText = 'color:#8f98a0; white-space:nowrap; flex-shrink:0; text-align:right; font-family:monospace; min-width:70px;';

                        mItem.appendChild(mLeftSide);
                        mItem.appendChild(mSizeSpan);
                        manifestsList.appendChild(mItem);
                    });
                    manifestsSection.appendChild(manifestsList);
                    contentContainer.appendChild(manifestsSection);
                }

                item.appendChild(contentContainer);
                fileList.appendChild(item);
            });

            // 3. Render all other standalone files
            standaloneFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                var style = badgeStyles[cat];

                var fileDisplayName = 'Unknown';
                var fileAppId = getAppIdFromFileName(file.name);

                if (fileAppId) {
                    if (fetchedMetadataCache[fileAppId]) {
                        var appData = fetchedMetadataCache[fileAppId];
                        var name = appData.common && appData.common.name || fileAppId;
                        var parentName = activeAppMetadata && activeAppMetadata.common && activeAppMetadata.common.name;
                        if (parentName && name.indexOf(parentName) === 0) {
                            var sub = name.substring(parentName.length).trim();
                            sub = sub.replace(/^[:\-\s]+/, '').replace(/^DLC\s*[:\-\s]*/i, '').trim();
                            if (sub) name = sub;
                        }
                        fileDisplayName = name;
                    } else {
                        fetchAppMetadata(fileAppId);
                        fileDisplayName = 'AppID ' + fileAppId;
                    }
                } else {
                    fileDisplayName = file.name;
                }

                var isChecked = checkedFiles[file.name] !== false;

                var item = document.createElement('div');
                item.style.cssText = 'display:flex; justify-content:space-between; align-items:center; background:rgba(255,255,255,0.03); border:1px solid rgba(255,255,255,0.05); padding:20px 24px; border-radius:6px; font-size:14px; gap:12px; overflow:hidden; cursor:pointer; user-select:none; transition:opacity 0.15s, background 0.15s; flex-shrink:0; width:100%; box-sizing:border-box;' + (!isChecked ? ' opacity:0.4;' : '');

                var cb = document.createElement('input');
                cb.type = 'checkbox';
                cb.checked = isChecked;
                cb.style.cssText = 'width:16px; height:16px; flex-shrink:0; pointer-events:none; accent-color:#1a9fff;';
                item.appendChild(cb);

                (function (f) {
                    item.onclick = function (e) {
                        e.preventDefault(); e.stopPropagation();
                        checkedFiles[f.name] = checkedFiles[f.name] === false;
                        updateFileList();
                    };
                })(file);

                var leftSide = document.createElement('div');
                leftSide.style.cssText = 'display:flex; align-items:center; gap:8px; overflow:hidden; flex:1; min-width:0;';

                var badge = document.createElement('span');
                badge.innerText = style.label;
                badge.style.cssText = 'background:' + style.color + '; color:' + style.textColor + '; border:1px solid ' + style.color.replace('0.15', '0.3').replace('0.08', '0.2') + '; font-size:10px; font-weight:700; text-transform:uppercase; padding:2px 6px; border-radius:4px; letter-spacing:0.5px; white-space:nowrap; flex-shrink:0;';
                leftSide.appendChild(badge);

                var nameSpan = document.createElement('span');
                nameSpan.innerText = fileDisplayName;
                nameSpan.style.cssText = 'color:#fff; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex-shrink:1; min-width:0;';
                leftSide.appendChild(nameSpan);

                var sizeSpan = document.createElement('span');
                var sizeKB = Math.round(file.size / 1024 * 10) / 10;
                var sizeStr = sizeKB >= 1024 ? (Math.round(sizeKB / 1024 * 10) / 10) + ' MB' : sizeKB + ' KB';
                sizeSpan.innerText = sizeStr;
                sizeSpan.style.cssText = 'color:#8f98a0; white-space:nowrap; flex-shrink:0; text-align:right; font-family:monospace; min-width:85px;';

                item.appendChild(leftSide);
                item.appendChild(sizeSpan);
                fileList.appendChild(item);
            });

            // Update the submit button state after rendering
            updateSubmitButton();
        }

        submitBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();

            var appid = detectAppIdFromCheckedFiles();
            if (!appid) {
                status.style.color = '#ff4d4d';
                status.innerText = 'Error: Could not detect AppID from files. Ensure a .lua file with addappid() is included.';
                return;
            }

            var filesToInstall = selectedFiles.filter(function (f) { return checkedFiles[f.name] !== false; });
            if (filesToInstall.length === 0) {
                status.style.color = '#ff4d4d';
                status.innerText = 'Error: Please select at least one file to import.';
                return;
            }

            submitBtn.innerText = 'Processing...';
            submitBtn.disabled = true;
            submitBtn.style.background = 'rgba(255, 255, 255, 0.05)';
            submitBtn.style.color = 'rgba(255,255,255,0.3)';
            submitBtn.style.cursor = 'not-allowed';
            status.style.color = '#8f98a0';
            status.innerText = 'Reading files...';

            var promises = filesToInstall.map(function (file) {
                return new Promise(function (resolve, reject) {
                    var reader = new FileReader();
                    reader.onload = function () {
                        var base64 = reader.result.split(',')[1];
                        resolve({ name: file.webkitRelativePath || file.name, content: base64 });
                    };
                    reader.onerror = function (err) { reject(err); };
                    reader.readAsDataURL(file);
                });
            });

            Promise.all(promises)
                .then(function (filesData) {
                    status.innerText = 'Installing to Steam...';
                    var payload = {
                        appid: appid,
                        files: filesData
                    };

                    return fetch('http://127.0.0.1:9001/manual-install', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(payload)
                    });
                })
                .then(function (r) { return r.json(); })
                .then(function (res) {
                    submitBtn.innerText = 'Import';

                    if (res.success) {
                        status.style.color = '#4caf50';
                        status.innerText = 'Success: ' + (res.message || 'Files installed successfully!');

                        // Find all distinct AppIDs from the checked lua files to trigger installations
                        var appidsToInstall = [];
                        selectedFiles.forEach(function (f) {
                            if (checkedFiles[f.name] !== false && f.name.toLowerCase().endsWith('.lua')) {
                                var content = luaContentsCache[f.name];
                                var id = getAppIdFromLuaContent(content) || getAppIdFromFileName(f.name);
                                if (id && appidsToInstall.indexOf(id) === -1) {
                                    appidsToInstall.push(id);
                                }
                            }
                        });

                        // Fallback to the main appid if no custom ones are found in selected lua files
                        if (appidsToInstall.length === 0 && appid) {
                            appidsToInstall.push(appid);
                        }

                        selectedFiles = [];
                        checkedFiles = {};
                        updateFileList();

                        // Trigger the installation window in Steam for each AppID
                        appidsToInstall.forEach(function (id, idx) {
                            setTimeout(function () {
                                window.location.href = 'steam://install/' + id;
                            }, idx * 500);
                        });
                    } else {
                        status.style.color = '#ff4d4d';
                        status.innerText = 'Failed: ' + (res.message || 'Unknown error');
                        updateSubmitButton();
                    }
                })
                .catch(function (err) {
                    submitBtn.innerText = 'Import';
                    status.style.color = '#ff4d4d';
                    status.innerText = 'Error connecting to backend: ' + err.message;
                    updateSubmitButton();
                });
        };
    }

    function addSlsTab() {
        var container = findSuperNavContainer();
        if (!container) return;

        if (document.getElementById('sls-top-tab')) return;

        // Find one of the existing tab divs (Store, Community, etc.) to clone
        var siblingTab = container.querySelector('.' + STEAM_TAB_BASE_CLASS);
        if (!siblingTab) {
            // Last resort: pick a child that is a DIV
            for (var i = 0; i < container.children.length; i++) {
                if (container.children[i].tagName === 'DIV') {
                    siblingTab = container.children[i];
                    break;
                }
            }
        }
        if (!siblingTab) return;

        // Clone the tab and strip inherited event listeners by round-tripping through HTML
        var rawClone = siblingTab.cloneNode(true);
        var slsTab = document.createElement('div');
        slsTab.className = rawClone.className;
        slsTab.innerHTML = rawClone.innerHTML;
        slsTab.id = 'sls-top-tab';
        // Remove active class if cloned tab had it
        slsTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);

        // Set the text to 'Import Lua files' inside the known text container class
        var textEl = slsTab.querySelector('.' + STEAM_TAB_TEXT_CLASS);
        if (textEl) {
            textEl.textContent = 'Import Lua files';
        } else {
            // Fallback: find a span or the deepest text node
            textEl = slsTab.querySelector('span') || slsTab.querySelector('div') || slsTab;
            textEl.textContent = 'Import Lua files';
        }

        // Remove any href/navigation attributes from child anchors
        var anchors = slsTab.querySelectorAll('a');
        for (var ai = 0; ai < anchors.length; ai++) {
            anchors[ai].removeAttribute('href');
            anchors[ai].style.cursor = 'pointer';
        }

        container.appendChild(slsTab);
        log('Added Import Lua files tab to top bar');

        slsTab.onclick = function (e) {
            e.preventDefault();
            e.stopPropagation();
            showSlsPage();
        };
    }

    function initSlsTabAndPage() {
        var container = findSuperNavContainer();
        if (container) {
            addSlsTab();

            // Set up a listener to hide the SLS page if any other tab is clicked
            if (!container.__slsTabListenerAttached) {
                container.__slsTabListenerAttached = true;
                container.addEventListener('click', function (e) {
                    var target = e.target;
                    while (target && target !== container) {
                        if (target.classList && target.classList.contains(STEAM_TAB_BASE_CLASS)) {
                            if (target.id !== 'sls-top-tab') {
                                hideSlsPage();
                            }
                            break;
                        }
                        target = target.parentNode;
                    }
                });
            }
        }
    }

    var debounceTimer = null;
    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function () {
            debounceTimer = null;
            initSlsTabAndPage();
        });
    }

    var observer = new MutationObserver(debouncedAddButtons);
    if (document.body) observer.observe(document.body, { childList: true, subtree: true });
    initSlsTabAndPage();
})();