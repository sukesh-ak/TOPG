// Multi-connection GPU monitoring system
let connections = new Map(); // connectionId -> connection object
let charts = new Map(); // connectionId -> chart instance
let connectionIdCounter = 0;

// UI Elements
const settingsModal = document.getElementById("settingsModal");
const gpuGrid = document.getElementById("gpuGrid");
const noConnections = document.getElementById("noConnections");
const connectionsList = document.getElementById("connectionsList");

// Connection Management Functions
function addConnection() {
    const host = document.getElementById('newHost').value.trim();
    const port = document.getElementById('newPort').value.trim();
    const name = document.getElementById('newName').value.trim();

    if (!host || !port || !name) {
        alert('Please fill in all fields');
        return;
    }

    const connectionId = ++connectionIdCounter;
    const connection = {
        id: connectionId,
        name: name,
        host: host,
        port: parseInt(port),
        ws: null,
        isConnected: false,
        gpus: new Map() // Map of gpuIndex -> gpu data
    };

    connections.set(connectionId, connection);

    // Clear input fields
    document.getElementById('newHost').value = '';
    document.getElementById('newPort').value = '8080';
    document.getElementById('newName').value = '';

    saveConnectionsToStorage();
    updateConnectionsList();
    updateGpuGrid();
}

function removeConnection(connectionId) {
    const connection = connections.get(connectionId);
    if (connection && connection.ws) {
        connection.ws.close();
    }
    connections.delete(connectionId);
    charts.delete(connectionId);

    saveConnectionsToStorage();
    updateConnectionsList();
    updateGpuGrid();
}

function connectToSystem(connectionId) {
    const connection = connections.get(connectionId);
    if (!connection || connection.isConnected) return;

    const wsUrl = `ws://${connection.host}:${connection.port}`;

    try {
        connection.ws = new WebSocket(wsUrl);

        connection.ws.onopen = () => {
            connection.isConnected = true;
            updateConnectionsList();
            updateGpuGrid();

            // Start requesting GPU data
            if (connection.ws.readyState === WebSocket.OPEN) {
                connection.ws.send("/live");
            }
        };

        connection.ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                // Ignore command response messages
                if (data.status) {
                    return;
                }
                if (Array.isArray(data)) {
                    data.forEach(gpuData => {
                        if (gpuData) {
                            processGpuData(connectionId, gpuData);
                        }
                    });
                } else {
                    processGpuData(connectionId, data);
                }
            } catch (e) {
                console.error('Error parsing GPU data:', e);
            }
        };

        connection.ws.onclose = () => {
            connection.isConnected = false;
            connection.ws = null;
            if (connection.interval) {
                clearInterval(connection.interval);
                connection.interval = null;
            }
            updateConnectionsList();
            updateGpuGrid();
        };

        connection.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
            connection.isConnected = false;
            updateConnectionsList();
            updateGpuGrid();
        };

    } catch (error) {
        console.error('Failed to connect:', error);
    }
}

function disconnectFromSystem(connectionId) {
    const connection = connections.get(connectionId);
    if (connection && connection.ws) {
        connection.ws.close();
        connection.isConnected = false;
        connection.ws = null;
        if (connection.interval) {
            clearInterval(connection.interval);
            connection.interval = null;
        }
        updateConnectionsList();
        updateGpuGrid();
    }
}

function connectAll() {
    connections.forEach((connection, id) => {
        if (!connection.isConnected) {
            connectToSystem(id);
        }
    });
}

function disconnectAll() {
    connections.forEach((connection, id) => {
        if (connection.isConnected) {
            disconnectFromSystem(id);
        }
    });
}

// UI Update Functions
function updateConnectionsList() {
    connectionsList.innerHTML = '';

    connections.forEach((connection, id) => {
        const item = document.createElement('div');
        item.className = `connection-item ${connection.isConnected ? 'connected' : ''}`;
        item.innerHTML = `
            <div class="connection-info">
                <div class="connection-name">${connection.name}</div>
                <div class="connection-address">${connection.host}:${connection.port}</div>
            </div>
            <div class="connection-status-indicator ${connection.isConnected ? 'connected' : 'disconnected'}">
                ${connection.isConnected ? 'Connected' : 'Disconnected'}
            </div>
            <div class="connection-actions-small">
                <button class="btn-small btn-connect" onclick="connectToSystem(${id})" ${connection.isConnected ? 'disabled' : ''}>Connect</button>
                <button class="btn-small btn-disconnect" onclick="disconnectFromSystem(${id})" ${!connection.isConnected ? 'disabled' : ''}>Disconnect</button>
                <button class="btn-small btn-remove" onclick="removeConnection(${id})" ${connection.isConnected ? 'disabled' : ''}>Remove</button>
            </div>
        `;
        connectionsList.appendChild(item);
    });
}

function updateGpuGrid() {
    // Clear existing GPU cards
    const existingCards = gpuGrid.querySelectorAll('.gpu-card');
    existingCards.forEach(card => card.remove());

    if (connections.size === 0) {
        noConnections.style.display = 'flex';
    } else {
        noConnections.style.display = 'none';

        connections.forEach((connection, id) => {
            if (connection.gpus.size === 0) {
                // Create a placeholder card if no GPUs detected yet
                createGpuCard(connection, null);
            } else {
                // Create a card for each GPU
                connection.gpus.forEach((gpu, gpuIndex) => {
                    createGpuCard(connection, gpuIndex);
                });
            }
        });
    }
}

function createGpuCard(connection, gpuIndex) {
    const card = document.createElement('div');
    card.className = `gpu-card ${connection.isConnected ? 'connected' : ''}`;
    const cardId = gpuIndex !== null ? `gpu-card-${connection.id}-${gpuIndex}` : `gpu-card-${connection.id}`;
    card.id = cardId;

    const gpu = gpuIndex !== null ? connection.gpus.get(gpuIndex) : null;
    const gpuName = gpu ? gpu.name : 'Waiting for data...';
    const gpuIndexLabel = gpuIndex !== null ? ` (GPU ${gpuIndex})` : '';

    card.innerHTML = `
        <div class="gpu-card-header">
            <div class="gpu-card-title">
                <div class="gpu-card-name">${connection.name}${gpuIndexLabel}</div>
                <div class="gpu-card-status" style="display: ${connection.isConnected ? 'inline' : 'none'}">🟢</div>
            </div>
            <div class="gpu-card-connection">${connection.host}:${connection.port}</div>
        </div>
        <div class="gpu-card-body">
            <div class="gpu-metrics">
                <div class="metric-item">
                    <div class="metric-label">GPU</div>
                    <div class="metric-value" id="gpu-util-${connection.id}-${gpuIndex}">--</div>
                </div>
                <div class="metric-item">
                    <div class="metric-label">Memory</div>
                    <div class="metric-value" id="memory-util-${connection.id}-${gpuIndex}">--</div>
                </div>
                <div class="metric-item">
                    <div class="metric-label">Temp</div>
                    <div class="metric-value" id="temp-${connection.id}-${gpuIndex}">--</div>
                </div>
            </div>
            <div class="gpu-chart-container">
                <canvas id="chart-${connection.id}-${gpuIndex}"></canvas>
            </div>
            <div class="gpu-info-row">
                <div class="gpu-info-item" id="gpu-name-${connection.id}-${gpuIndex}">${gpuName}</div>
                <div class="gpu-info-item" id="gpu-memory-${connection.id}-${gpuIndex}">Memory: --</div>
            </div>
        </div>
    `;

    gpuGrid.appendChild(card);

    // Initialize chart for this GPU
    if (gpuIndex !== null) {
        initGpuChart(connection.id, gpuIndex);
    }
}

// Data Processing Functions
function processGpuData(connectionId, item) {
    const connection = connections.get(connectionId);
    if (!connection) return;

    const gpuIndex = item["index"] || 0;
    console.log(`=== Processing GPU ${gpuIndex} data for ${connection.name} ===`);

    // Parse GPU data
    const gpuUtil = item["utilization.gpu"] || 0;
    const memUsed = item["memory.used"] || 0;
    const memTotal = item["memory.total"] || 1;
    const memPercent = Math.round((memUsed / memTotal) * 100);
    const temp = item["temperature.gpu"] || 0;
    const gpuName = item["name"] || "Unknown GPU";

    // Get or create GPU data for this index
    const isNewGpu = !connection.gpus.has(gpuIndex);
    if (isNewGpu) {
        connection.gpus.set(gpuIndex, {
            index: gpuIndex,
            name: gpuName,
            totalMemory: memTotal,
            data: {
                utilizationData: [],
                memoryData: [],
                tempData: []
            }
        });
        // Refresh the GPU grid to add the new GPU card
        updateGpuGrid();
    }

    const gpu = connection.gpus.get(gpuIndex);

    // Update GPU info
    gpu.name = gpuName;
    gpu.totalMemory = memTotal;

    // Update data arrays
    gpu.data.utilizationData.push(gpuUtil);
    gpu.data.memoryData.push(memPercent);
    gpu.data.tempData.push(temp);

    // Limit chart history to last 50 points
    if (gpu.data.utilizationData.length > 50) gpu.data.utilizationData.shift();
    if (gpu.data.memoryData.length > 50) gpu.data.memoryData.shift();
    if (gpu.data.tempData.length > 50) gpu.data.tempData.shift();

    // Update UI for this GPU
    updateGpuMetrics(connectionId, gpuIndex, gpuUtil, memPercent, temp);
    updateGpuChart(connectionId, gpuIndex);
    updateGpuInfo(connectionId, gpuIndex, gpuName, memTotal);
}

function updateGpuMetrics(connectionId, gpuIndex, gpuUtil, memPercent, temp) {
    const gpuUtilEl = document.getElementById(`gpu-util-${connectionId}-${gpuIndex}`);
    const memoryUtilEl = document.getElementById(`memory-util-${connectionId}-${gpuIndex}`);
    const tempEl = document.getElementById(`temp-${connectionId}-${gpuIndex}`);

    if (gpuUtilEl) gpuUtilEl.textContent = `${gpuUtil}%`;
    if (memoryUtilEl) memoryUtilEl.textContent = `${memPercent}%`;
    if (tempEl) tempEl.textContent = `${temp}°C`;

    // Update connection status indicator
    const card = document.getElementById(`gpu-card-${connectionId}-${gpuIndex}`);
    const statusIndicator = card?.querySelector('.gpu-card-status');
    if (statusIndicator) {
        statusIndicator.style.display = 'inline';
    }
    if (card) {
        card.classList.add('connected');
    }
}

function updateGpuInfo(connectionId, gpuIndex, gpuName, memTotal) {
    const totalMemoryGB = Math.round(memTotal / 1024 * 100) / 100;
    const gpuNameEl = document.getElementById(`gpu-name-${connectionId}-${gpuIndex}`);
    const gpuMemoryEl = document.getElementById(`gpu-memory-${connectionId}-${gpuIndex}`);

    if (gpuNameEl) gpuNameEl.textContent = gpuName;
    if (gpuMemoryEl) gpuMemoryEl.textContent = `Memory: ${totalMemoryGB} GB`;
}

// Chart Functions
function getThemeColors() {
    const isDark = document.body.hasAttribute('data-theme') && document.body.getAttribute('data-theme') === 'dark';
    return {
        text: isDark ? '#e0e0e0' : '#333333',
        gridLines: isDark ? '#404040' : '#e0e0e0',
        gpu: '#2980b9',
        memory: '#e74c3c'
    };
}

function initGpuChart(connectionId, gpuIndex) {
    const canvas = document.getElementById(`chart-${connectionId}-${gpuIndex}`);
    if (!canvas) return;

    const colors = getThemeColors();
    const ctx = canvas.getContext('2d');

    const chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'GPU %',
                data: [],
                borderColor: colors.gpu,
                backgroundColor: 'rgba(41, 128, 185, 0.1)',
                tension: 0.3,
                fill: false,
                borderWidth: 2
            }, {
                label: 'Memory %',
                data: [],
                borderColor: colors.memory,
                backgroundColor: 'rgba(231, 76, 60, 0.1)',
                tension: 0.3,
                fill: false,
                borderWidth: 2
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    display: true,
                    position: 'top',
                    labels: {
                        color: colors.text,
                        font: { size: 10 }
                    }
                },
                tooltip: {
                    callbacks: {
                        title: function () { return ''; },
                        label: function (context) {
                            return context.dataset.label + ': ' + context.parsed.y + '%';
                        }
                    }
                }
            },
            scales: {
                y: {
                    min: 0,
                    max: 100,
                    ticks: { color: colors.text, font: { size: 10 } },
                    grid: { color: colors.gridLines }
                },
                x: { display: false }
            },
            animation: { duration: 0 }
        }
    });

    const chartKey = `${connectionId}-${gpuIndex}`;
    charts.set(chartKey, chart);
}

function updateGpuChart(connectionId, gpuIndex) {
    const chartKey = `${connectionId}-${gpuIndex}`;
    const chart = charts.get(chartKey);
    const connection = connections.get(connectionId);

    if (!chart || !connection) return;

    const gpu = connection.gpus.get(gpuIndex);
    if (!gpu) return;

    // Update chart data
    chart.data.datasets[0].data = [...gpu.data.utilizationData];
    chart.data.datasets[1].data = [...gpu.data.memoryData];

    // Generate labels
    chart.data.labels = gpu.data.utilizationData.map((_, i) => i);

    chart.update('none');
}

function updateAllChartColors() {
    charts.forEach((chart, connectionId) => {
        if (!chart) return;

        const colors = getThemeColors();
        chart.options.plugins.legend.labels.color = colors.text;
        chart.options.scales.y.ticks.color = colors.text;
        chart.options.scales.y.grid.color = colors.gridLines;
        chart.update('none');
    });
}

// Modal functions
function openSettingsModal() {
    settingsModal.classList.add('show');
    document.body.style.overflow = 'hidden';
    updateConnectionsList();
}

function closeSettingsModal(event) {
    if (!event || event.target === settingsModal) {
        settingsModal.classList.remove('show');
        document.body.style.overflow = '';
    }
}

// Theme management
function initTheme() {
    const savedTheme = localStorage.getItem('theme') || 'system';
    applyTheme(savedTheme);
    updateThemeToggleIcon();
}

function toggleTheme() {
    const currentTheme = localStorage.getItem('theme') || 'system';
    let nextTheme;

    // Cycle through: system -> light -> dark -> system
    switch (currentTheme) {
        case 'system':
            nextTheme = 'light';
            break;
        case 'light':
            nextTheme = 'dark';
            break;
        case 'dark':
            nextTheme = 'system';
            break;
        default:
            nextTheme = 'system';
    }

    localStorage.setItem('theme', nextTheme);
    applyTheme(nextTheme);
    updateThemeToggleIcon();
}

function updateThemeToggleIcon() {
    const themeToggle = document.getElementById('themeToggle');
    const currentTheme = localStorage.getItem('theme') || 'system';

    if (themeToggle) {
        switch (currentTheme) {
            case 'system':
                themeToggle.innerHTML = '🖥️';
                themeToggle.title = 'Theme: System (click for Light)';
                break;
            case 'light':
                themeToggle.innerHTML = '☀️';
                themeToggle.title = 'Theme: Light (click for Dark)';
                break;
            case 'dark':
                themeToggle.innerHTML = '🌙';
                themeToggle.title = 'Theme: Dark (click for System)';
                break;
        }
    }
}

function applyTheme(theme) {
    const body = document.body;
    body.removeAttribute('data-theme');

    if (theme === 'system') {
        if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
            body.setAttribute('data-theme', 'dark');
        }
    } else if (theme === 'dark') {
        body.setAttribute('data-theme', 'dark');
    }

    // Update chart colors when theme changes
    updateAllChartColors();
}

// Event listeners
document.addEventListener('keydown', function (event) {
    if (event.key === 'Escape' && settingsModal.classList.contains('show')) {
        closeSettingsModal();
    }
});

// Listen for system theme changes
if (window.matchMedia) {
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
        const currentTheme = localStorage.getItem('theme') || 'system';
        if (currentTheme === 'system') {
            applyTheme('system');
        }
    });
}

// Storage Functions
function saveConnectionsToStorage() {
    const connectionsData = [];
    connections.forEach((connection, id) => {
        connectionsData.push({
            id: connection.id,
            name: connection.name,
            host: connection.host,
            port: connection.port
        });
    });
    localStorage.setItem('gpuConnections', JSON.stringify(connectionsData));
    localStorage.setItem('connectionIdCounter', connectionIdCounter.toString());
}

function loadConnectionsFromStorage() {
    try {
        const savedConnections = localStorage.getItem('gpuConnections');
        const savedCounter = localStorage.getItem('connectionIdCounter');

        if (savedConnections) {
            const connectionsData = JSON.parse(savedConnections);
            connections.clear(); // Clear existing connections

            connectionsData.forEach(connData => {
                const connection = {
                    id: connData.id,
                    name: connData.name,
                    host: connData.host,
                    port: connData.port,
                    ws: null,
                    isConnected: false,
                    gpus: new Map() // Initialize empty GPU map
                };
                connections.set(connData.id, connection);
            });
        }

        if (savedCounter) {
            connectionIdCounter = parseInt(savedCounter);
        }
    } catch (error) {
        console.error('Error loading connections from storage:', error);
    }
}

// Initialize everything on load
window.onload = () => {
    initTheme();
    loadConnectionsFromStorage(); // Load saved connections
    updateGpuGrid(); // Show connections or empty state
};