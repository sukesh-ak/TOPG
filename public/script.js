// WebSocket connection
let ws;
let utilizationData = [];
let memoryData = [];
let tempData = [];
let isLive = false;

const statusEl = document.getElementById("status");

// Initialize connection
function connect() {
    const url = "ws://localhost:8080";
    ws = new WebSocket(url);

    ws.onopen = () => {
        statusEl.textContent = "Connected! 🚀";
        console.log("Connected to server");
        requestGpu(); // Get initial data
    };

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            if (Array.isArray(data)) {
                // Multiple GPU entries (e.g., multiple cards)
                data.forEach(item => {
                    const index = item.index;
                    const gpuUtil = parseInt(item["utilization.gpu"]);
                    const memUsed = parseInt(item["memory.used"]);
                    const memTotal = parseInt(item["memory.total"]);
                    const temp = parseInt(item["temperature.gpu"]);

                    // Update charts
                    utilizationData.push(gpuUtil);
                    memoryData.push(memUsed);
                    tempData.push(temp);

                    // Limit chart history to last 100 points
                    if (utilizationData.length > 100) utilizationData.shift();
                    if (memoryData.length > 100) memoryData.shift();
                    if (tempData.length > 100) tempData.shift();

                    updateCharts();
                });
            } else {
                // Single entry
                const item = data;
                const gpuUtil = parseInt(item["utilization.gpu"]);
                const memUsed = parseInt(item["memory.used"]);
                const memTotal = parseInt(item["memory.total"]);
                const temp = parseInt(item["temperature.gpu"]);

                utilizationData.push(gpuUtil);
                memoryData.push(memUsed);
                tempData.push(temp);

                if (utilizationData.length > 100) utilizationData.shift();
                if (memoryData.length > 100) memoryData.shift();
                if (tempData.length > 100) tempData.shift();

                updateCharts();
            }
        } catch (e) {
            console.error("Error parsing data:", e);
        }
    };

    ws.onclose = () => {
        statusEl.textContent = "Disconnected. Reconnecting...";
        setTimeout(connect, 2000);
    };

    ws.onerror = (err) => {
        console.error("WebSocket error:", err);
        statusEl.textContent = "Error: " + err;
    };
}

// Request current GPU data
function requestGpu() {
    ws.send("/gpu");
}

// Start live streaming
function startLive() {
    if (isLive) return;
    isLive = true;
    ws.send("/live");
    statusEl.textContent = "Live streaming started. Updates every 1s.";
}

// Stop live streaming
function stopLive() {
    if (!isLive) return;
    isLive = false;
    ws.send("/stop");
    statusEl.textContent = "Live streaming stopped.";
}

// Update charts
function updateCharts() {
    const ctxUtil = document.getElementById("utilizationChart").getContext("2d");
    const ctxMem = document.getElementById("memoryChart").getContext("2d");
    const ctxTemp = document.getElementById("tempChart").getContext("2d");

    // Clear previous charts
    new Chart(ctxUtil, {
        type: 'line',
        data: {
            labels: Array.from({ length: utilizationData.length }, (_, i) => i),
            datasets: [{
                label: 'GPU Utilization (%)',
                data: utilizationData,
                borderColor: '#2980b9',
                backgroundColor: 'rgba(41, 128, 185, 0.1)',
                tension: 0.3,
                fill: true
            }]
        },
        options: {
            responsive: true,
            plugins: { legend: { display: false } },
            scales: { y: { min: 0, max: 100 } }
        }
    });

    new Chart(ctxMem, {
        type: 'bar',
        data: {
            labels: Array.from({ length: memoryData.length }, (_, i) => i),
            datasets: [{
                label: 'Memory Used (MB)',
                data: memoryData,
                backgroundColor: '#e74c3c',
                borderColor: '#c0392b',
                borderWidth: 1
            }]
        },
        options: {
            responsive: true,
            plugins: { legend: { display: false } },
            scales: { y: { beginAtZero: true } }
        }
    });

    new Chart(ctxTemp, {
        type: 'line',
        data: {
            labels: Array.from({ length: tempData.length }, (_, i) => i),
            datasets: [{
                label: 'Temperature (°C)',
                data: tempData,
                borderColor: '#f39c12',
                backgroundColor: 'rgba(243, 156, 18, 0.1)',
                tension: 0.3,
                fill: true
            }]
        },
        options: {
            responsive: true,
            plugins: { legend: { display: false } },
            scales: { y: { min: 0, max: 100 } }
        }
    });
}

// Auto-connect on load
window.onload = () => {
    connect();
};

// Optional: Refresh every 10 seconds if needed
setInterval(() => {
    if (isLive) requestGpu();
}, 10000);