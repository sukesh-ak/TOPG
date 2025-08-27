// WebSocket connection
let ws;
let utilizationData = [];
let memoryData = [];
let tempData = [];
let isLive = false;
let charts = {}; // Store chart instances
let isConnected = false;
let isExpanded = true;

const statusEl = document.getElementById("status");
const hostEl = document.getElementById("host");
const portEl = document.getElementById("port");
const connectionUrlEl = document.getElementById("connectionUrl");
const connectionStatus = document.getElementById("connectionStatus");
const settingsModal = document.getElementById("settingsModal");
const connectionIndicator = document.getElementById("connectionIndicator");

// Get button elements
const connectBtn = document.querySelector('button[onclick="connect()"]');
const disconnectBtn = document.querySelector('button[onclick="disconnect()"]');

// Update connection URL display
function updateConnectionUrl() {
    const host = hostEl.value || "localhost";
    const port = portEl.value || "8080";
    if (isConnected) {
        connectionUrlEl.innerHTML = `Connected: <strong>ws://${host}:${port}</strong>`;
        connectionUrlEl.style.color = "#27ae60";
        connectionStatus.innerHTML = "🟢 Connected";
        connectionIndicator.style.display = "inline";
        connectionIndicator.innerHTML = "🟢";
        
        // Disable connect button, enable disconnect button
        if (connectBtn) {
            connectBtn.disabled = true;
            connectBtn.style.opacity = "0.5";
            connectBtn.style.cursor = "not-allowed";
        }
        if (disconnectBtn) {
            disconnectBtn.disabled = false;
            disconnectBtn.style.opacity = "1";
            disconnectBtn.style.cursor = "pointer";
        }
    } else {
        connectionUrlEl.innerHTML = `ws://${host}:${port}`;
        connectionUrlEl.style.color = "#666";
        connectionStatus.innerHTML = "🔴 Disconnected";
        connectionIndicator.style.display = "none";
        
        // Enable connect button, disable disconnect button
        if (connectBtn) {
            connectBtn.disabled = false;
            connectBtn.style.opacity = "1";
            connectBtn.style.cursor = "pointer";
        }
        if (disconnectBtn) {
            disconnectBtn.disabled = true;
            disconnectBtn.style.opacity = "0.5";
            disconnectBtn.style.cursor = "not-allowed";
        }
    }
}

// Modal functions
function openSettingsModal() {
    settingsModal.classList.add('show');
    document.body.style.overflow = 'hidden';
}

function closeSettingsModal(event) {
    if (!event || event.target === settingsModal) {
        settingsModal.classList.remove('show');
        document.body.style.overflow = '';
    }
}

// Close modal with Escape key
document.addEventListener('keydown', function(event) {
    if (event.key === 'Escape' && settingsModal.classList.contains('show')) {
        closeSettingsModal();
    }
});

// Initialize connection
function connect() {
    if (ws && (ws.readyState === WebSocket.CONNECTING || ws.readyState === WebSocket.OPEN)) {
        console.log("Already connected or connecting");
        return;
    }

    const host = hostEl.value || "localhost";
    const port = portEl.value || "8080";
    const url = `ws://${host}:${port}`;
    
    statusEl.textContent = "Connecting...";
    ws = new WebSocket(url);

    ws.onopen = () => {
        statusEl.textContent = "Connected! 🚀";
        isConnected = true;
        updateConnectionUrl();
        console.log("Connected to server");
        // Auto-start live streaming
        setTimeout(() => {
            startLive();
        }, 500);
    };

    ws.onmessage = (event) => {
        console.log("=== RAW WebSocket message ===");
        console.log("Raw event.data:", event.data);
        console.log("Data length:", event.data.length);
        console.log("Data type:", typeof event.data);
        
        try {
            const data = JSON.parse(event.data);
            console.log("Parsed JSON successfully");
            
            // Handle status messages
            if (data.status) {
                console.log("Status message:", data);
                if (data.status === "connected") {
                    statusEl.textContent = "Connected! 🚀 " + (data.help || "");
                } else if (data.status === "live") {
                    statusEl.textContent = "Live streaming started. Updates every 1s.";
                } else if (data.status === "stopped") {
                    statusEl.textContent = "Live streaming stopped.";
                }
                return;
            }

            console.log("GPU data message");
            if (Array.isArray(data)) {
                console.log("Array data with", data.length, "items");
                // Multiple GPU entries (e.g., multiple cards) - use first GPU
                if (data.length > 0) {
                    processGpuData(data[0]);
                }
            } else if (data.index !== undefined) {
                console.log("Single GPU data");
                // Single GPU entry
                processGpuData(data);
            } else {
                console.log("Unknown data format:", data);
            }
        } catch (e) {
            console.error("JSON Parse Error:", e);
            console.error("Raw data that failed to parse:", event.data);
            statusEl.textContent = "Error parsing server data";
        }
    };

    ws.onclose = () => {
        statusEl.textContent = "Disconnected";
        isConnected = false;
        isLive = false;
        updateConnectionUrl();
        console.log("Connection closed");
    };

    ws.onerror = (err) => {
        console.error("WebSocket error:", err);
        statusEl.textContent = "Connection error";
        isConnected = false;
        updateConnectionUrl();
    };
}

// Disconnect function
function disconnect() {
    if (ws) {
        ws.close();
        isConnected = false;
        isLive = false;
        statusEl.textContent = "Disconnected";
        updateConnectionUrl();
    }
}

// Process GPU data
function processGpuData(item) {
    console.log("=== Client received GPU data ===");
    console.log("Raw item:", JSON.stringify(item, null, 2));
    
    // Server now sends numeric values, so we can use them directly
    const gpuUtil = item["utilization.gpu"] || 0;
    const memUsed = item["memory.used"] || 0;
    const memTotal = item["memory.total"] || 1;
    const memPercent = Math.round((memUsed / memTotal) * 100);
    const temp = item["temperature.gpu"] || 0;
    const gpuName = item["name"] || "Unknown GPU";

    console.log("Parsed values:", { gpuUtil, memPercent, temp, gpuName });
    console.log("Temperature:", temp, "(from field:", item["temperature.gpu"], ")");

    // Update GPU info (only once when first connected)
    updateGpuInfo(gpuName, memTotal);

    // Update value displays
    updateValues(gpuUtil, memPercent, temp);

    // Update data arrays
    utilizationData.push(gpuUtil);
    memoryData.push(memPercent);
    tempData.push(temp);

    // Limit chart history to last 50 points
    if (utilizationData.length > 50) utilizationData.shift();
    if (memoryData.length > 50) memoryData.shift();
    if (tempData.length > 50) tempData.shift();

    updateCharts();
}

// Request current GPU data
function requestGpu() {
    if (!isConnected || !ws) {
        statusEl.textContent = "Not connected";
        return;
    }
    ws.send("/gpu");
}

// Start live streaming
function startLive() {
    if (!isConnected || !ws) {
        statusEl.textContent = "Not connected";
        return;
    }
    if (isLive) return;
    isLive = true;
    ws.send("/live");
}

// Stop live streaming
function stopLive() {
    if (!isConnected || !ws) {
        statusEl.textContent = "Not connected";
        return;
    }
    if (!isLive) return;
    isLive = false;
    ws.send("/stop");
}

// Initialize charts
function getThemeColors() {
    const isDark = document.body.hasAttribute('data-theme') && document.body.getAttribute('data-theme') === 'dark';
    return {
        text: isDark ? '#e0e0e0' : '#333333',
        gridLines: isDark ? '#404040' : '#e0e0e0',
        gpu: '#2980b9',
        memory: '#e74c3c'
    };
}

function initCharts() {
    const colors = getThemeColors();
    const ctxCombined = document.getElementById("combinedChart").getContext("2d");

    charts.combined = new Chart(ctxCombined, {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'GPU Utilization (%)',
                data: [],
                borderColor: colors.gpu,
                backgroundColor: 'rgba(41, 128, 185, 0.1)',
                tension: 0.3,
                fill: false,
                borderWidth: 2
            }, {
                label: 'Memory Usage (%)',
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
            plugins: { 
                legend: { 
                    display: true,
                    position: 'top',
                    labels: {
                        color: colors.text,
                        font: {
                            size: 12
                        }
                    }
                },
                tooltip: {
                    callbacks: {
                        title: function() {
                            return '';
                        },
                        label: function(context) {
                            return context.dataset.label + ': ' + context.parsed.y + '%';
                        }
                    }
                }
            },
            scales: { 
                y: { 
                    min: 0, 
                    max: 100,
                    grace: '5%',
                    ticks: {
                        color: colors.text
                    },
                    grid: {
                        color: colors.gridLines
                    }
                },
                x: { display: false }
            },
            animation: { duration: 0 }
        }
    });
}

function updateChartColors() {
    if (!charts.combined) return;
    
    const colors = getThemeColors();
    
    // Update legend colors
    charts.combined.options.plugins.legend.labels.color = colors.text;
    
    // Update scale colors
    charts.combined.options.scales.y.ticks.color = colors.text;
    charts.combined.options.scales.y.grid.color = colors.gridLines;
    
    charts.combined.update('none');
}

// Update temperature gauge
function updateTemperatureGauge(temp) {
    const gaugeFill = document.getElementById('gaugeFill');
    const tempValue = document.getElementById('tempValue');
    
    if (gaugeFill && tempValue) {
        // Calculate percentage for gauge (20°C = 0%, 100°C = 100%)
        const minTemp = 20;
        const maxTemp = 100;
        const percentage = Math.max(0, Math.min(100, ((temp - minTemp) / (maxTemp - minTemp)) * 100));
        
        gaugeFill.style.height = percentage + '%';
        tempValue.textContent = temp + '°C';
        
        // Change color based on temperature
        let color = '#4caf50'; // Green (cool)
        if (temp > 60) color = '#ffeb3b'; // Yellow (warm)
        if (temp > 70) color = '#ff9800'; // Orange (hot)
        if (temp > 80) color = '#f44336'; // Red (very hot)
        
        gaugeFill.style.background = color;
    }
}

// Update charts with new data
function updateCharts() {
    const labels = Array.from({ length: utilizationData.length }, (_, i) => i);

    // Update combined chart
    if (charts.combined) {
        charts.combined.data.labels = labels;
        charts.combined.data.datasets[0].data = [...utilizationData];
        charts.combined.data.datasets[1].data = [...memoryData];
        charts.combined.update('none');
    }
}

// Update GPU info display
function updateGpuInfo(name, totalMemoryMB) {
    const gpuNameEl = document.getElementById('gpuName');
    const totalMemoryEl = document.getElementById('totalMemory');
    
    if (gpuNameEl && name !== "Unknown GPU") {
        gpuNameEl.textContent = name;
    }
    
    if (totalMemoryEl && totalMemoryMB > 0) {
        const totalMemoryGB = Math.round(totalMemoryMB / 1024 * 100) / 100; // Convert to GB with 2 decimal places
        totalMemoryEl.textContent = `Total Memory: ${totalMemoryGB} GB`;
    }
}

// Update value displays
function updateValues(gpuUtil, memPercent, temp) {
    const gpuValueEl = document.getElementById('gpuValue');
    const memoryValueEl = document.getElementById('memoryValue');
    
    if (gpuValueEl) {
        gpuValueEl.textContent = `GPU: ${gpuUtil}%`;
    }
    
    if (memoryValueEl) {
        memoryValueEl.textContent = `Memory: ${memPercent}%`;
    }
    
    // Update temperature gauge
    updateTemperatureGauge(temp);
}

// Theme management
function initTheme() {
    const savedTheme = localStorage.getItem('theme') || 'system';
    const themeSelect = document.getElementById('themeSelect');
    themeSelect.value = savedTheme;
    applyTheme(savedTheme);
}

function changeTheme(theme) {
    localStorage.setItem('theme', theme);
    applyTheme(theme);
}

function applyTheme(theme) {
    const body = document.body;
    
    // Remove existing theme attributes
    body.removeAttribute('data-theme');
    
    if (theme === 'system') {
        // Use system preference
        if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
            body.setAttribute('data-theme', 'dark');
        }
        // Light theme is default (no attribute needed)
    } else if (theme === 'dark') {
        body.setAttribute('data-theme', 'dark');
    }
    // Light theme is default (no attribute needed)
    
    // Update chart colors when theme changes
    updateChartColors();
}

// Listen for system theme changes
if (window.matchMedia) {
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
        const currentTheme = localStorage.getItem('theme') || 'system';
        if (currentTheme === 'system') {
            applyTheme('system');
        }
    });
}

// Initialize everything on load
window.onload = () => {
    initCharts();
    updateConnectionUrl();
    initTheme();
    
    // Initialize connection indicator as hidden
    connectionIndicator.style.display = "none";
    
    // Add event listeners for host/port changes
    hostEl.addEventListener('input', updateConnectionUrl);
    portEl.addEventListener('input', updateConnectionUrl);
    
    // Auto-connect to localhost:8080
    connect();
};