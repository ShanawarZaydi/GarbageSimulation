const F6_CENTER = [33.7275, 73.0775];

const map = L.map("map", {
    center: F6_CENTER,
    zoom: 15,
    zoomControl: true
});

L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 19,
    attribution: "&copy; OpenStreetMap contributors"
}).addTo(map);

const roadLayer = L.layerGroup().addTo(map);
const routeLayer = L.layerGroup().addTo(map);
const truckLayer = L.layerGroup().addTo(map);
const requestLayer = L.layerGroup().addTo(map);
const disposalLayer = L.layerGroup().addTo(map);
const maintenanceLayer = L.layerGroup().addTo(map);

const truckMarkers = {};
const truckTargets = {};
const requestMarkers = {};
const collectedMarkerTimers = {};
const expiredCollectedMarkers = new Set();
let disposalMarker = null;
const chargingMarkers = {};
const routeAlgorithmSelect = document.getElementById("route-algorithm");
const mapActionSelect = document.getElementById("map-action");
const statusOverlay = document.getElementById("status-overlay");
const statusToggle = document.getElementById("status-toggle");
let routingUpdatePending = false;

statusToggle.addEventListener("click", () => {
    const collapsed = statusOverlay.classList.toggle("collapsed");
    statusToggle.setAttribute("aria-expanded", String(!collapsed));
    statusToggle.textContent = collapsed ? "Show details" : "Hide details";
});

function createCircleIcon(color, size, borderColor = "#111827", label = "") {
    return L.divIcon({
        className: "custom-marker",
        html: `<div style="
            width:${size}px;
            height:${size}px;
            border-radius:50%;
            background:${color};
            border:2px solid ${borderColor};
            box-shadow:0 1px 4px rgba(0,0,0,0.45);
            display:flex;
            align-items:center;
            justify-content:center;
            font-size:9px;
            font-weight:700;
            color:#111827;
        ">${label}</div>`,
        iconSize: [size, size],
        iconAnchor: [size / 2, size / 2]
    });
}

function formatTruckPopup(t) {
    const reqText = t.assignedRequestId > 0 ? `REQ-${t.assignedRequestId}` : "None";
    const reasonText = t.assignmentReason ? `<br><b>Note:</b> ${t.assignmentReason}` : "";
    return `
        <div>
            <b>${t.id}</b><br>
            <b>Status:</b> ${t.status}<br>
            <b>Node:</b> ${t.currentNode}<br>
            <b>Load:</b> ${t.currentLoad.toFixed(0)} / ${t.maxCapacity.toFixed(0)} kg<br>
            <b>Remaining:</b> ${t.remainingCapacity.toFixed(0)} kg<br>
            <b>Battery:</b> ${t.battery.toFixed(1)}%<br>
            <b>Assigned Request:</b> ${reqText}
            ${reasonText}
        </div>
    `;
}

function formatRequestPopup(r) {
    const truckText = r.assignedTruckId ? r.assignedTruckId : "Unassigned";
    return `
        <div>
            <b>${r.requestCode}</b><br>
            <b>Status:</b> ${r.status}<br>
            <b>Graph Node:</b> ${r.nodeId}<br>
            <b>Remaining / Total:</b> ${r.remainingQuantity.toFixed(0)} / ${r.totalQuantity.toFixed(0)} kg<br>
            <b>Assigned Truck:</b> ${truckText}<br>
            <b>Info:</b> ${r.statusNote}
        </div>
    `;
}

function scheduleCollectedMarkerRemoval(requestCode, cleanedAtUnixMs) {
    if (expiredCollectedMarkers.has(requestCode) ||
        Object.prototype.hasOwnProperty.call(collectedMarkerTimers, requestCode)) {
        return;
    }

    const remainingMs = Math.max(0, cleanedAtUnixMs + 10000 - Date.now());
    const removeMarker = () => {
        const marker = requestMarkers[requestCode];
        if (marker) {
            requestLayer.removeLayer(marker);
            delete requestMarkers[requestCode];
        }
        expiredCollectedMarkers.add(requestCode);
        delete collectedMarkerTimers[requestCode];
    };

    if (remainingMs === 0) {
        removeMarker();
        return;
    }
    collectedMarkerTimers[requestCode] = window.setTimeout(removeMarker, remainingMs);
}

function truckActivity(status) {
    const activities = {
        Available: "Patrolling",
        Assigned: "Assigned to a request",
        Travelling: "Going to collect garbage",
        Collecting: "Collecting garbage",
        Returning: "Going for disposal",
        Disposing: "Disposing garbage",
        GoingToCharge: "Going to charge",
        WaitingToCharge: "Waiting for charging bay",
        Charging: "Charging battery",
        LowBattery: "Stopped: critical battery",
        RoadBlocked: "Stopped: no open route"
    };
    return activities[status] || status;
}

function renderTruckStatuses(trucks) {
    const container = document.getElementById("truck-statuses");
    container.innerHTML = trucks.map((truck) => {
        const battery = Math.max(0, Math.min(100, truck.battery));
        const batteryColor = battery < 20 ? "#dc2626" : battery < 50 ? "#f59e0b" : "#16a34a";
        return `
            <div class="truck-card">
                <div class="truck-card-heading">
                    <span>${truck.id}</span>
                    <span>${battery.toFixed(0)}% battery</span>
                </div>
                <div class="battery-meter">
                    <div class="battery-meter-fill" style="width:${battery}%;background:${batteryColor}"></div>
                </div>
                <div class="truck-card-task">${truckActivity(truck.status)}</div>
                <div class="truck-card-details">
                    <span>Load</span>
                    <span>${truck.currentLoad.toFixed(0)} / ${truck.maxCapacity.toFixed(0)} kg</span>
                </div>
            </div>
        `;
    }).join("");
}

routeAlgorithmSelect.addEventListener("change", async () => {
    routingUpdatePending = true;
    routeAlgorithmSelect.disabled = true;
    try {
        const response = await fetch("/api/routing", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ algorithm: routeAlgorithmSelect.value })
        });
        const result = await response.json();
        if (!response.ok) {
            throw new Error(result.error || "Failed to update route algorithm");
        }
    } catch (err) {
        console.error("Failed to update route algorithm:", err);
    } finally {
        routingUpdatePending = false;
        routeAlgorithmSelect.disabled = false;
    }
});

async function loadRoadNetwork() {
    try {
        const res = await fetch("/api/graph");
        const data = await res.json();

        for (const edgePts of data.edges) {
            L.polyline(edgePts, {
                color: "#64748b",
                weight: 2,
                opacity: 0.35
            }).addTo(roadLayer);
        }

        if (data.disposalStation) {
            const d = data.disposalStation;
            disposalMarker = L.marker([d.lat, d.lon], {
                icon: createCircleIcon("#6d28d9", 20, "#ffffff", "D")
            }).addTo(disposalLayer);
            disposalMarker.bindPopup(`
                <div>
                    <b>${d.name}</b><br>
                    <b>Graph Node:</b> ${d.nodeId}<br>
                    <b>Coordinates:</b> ${d.lat.toFixed(5)}, ${d.lon.toFixed(5)}
                </div>
            `);
        }

        if (data.chargingStations && Array.isArray(data.chargingStations)) {
            data.chargingStations.forEach((c, idx) => {
                const m = L.marker([c.lat, c.lon], {
                    icon: createCircleIcon("#0284c7", 20, "#ffffff", `C${idx + 1}`)
                }).addTo(disposalLayer);
                m.bindPopup(`
                    <div>
                        <b>${c.name}</b><br>
                        <b>Graph Node:</b> ${c.nodeId}<br>
                        <b>Coordinates:</b> ${c.lat.toFixed(5)}, ${c.lon.toFixed(5)}
                    </div>
                `);
                chargingMarkers[c.nodeId] = m;
            });
        }
    } catch (err) {
        console.error("Failed to load graph:", err);
    }
}

async function pollState() {
    try {
        const res = await fetch("/api/state");
        const state = await res.json();

        document.getElementById("stat-trucks").textContent =
            `${state.stats.availableTrucks}/${state.stats.totalTrucks} avail`;
        document.getElementById("stat-active").textContent = state.stats.activeRequests;
        document.getElementById("stat-cleaned").textContent = state.stats.cleanedRequests;
        document.getElementById("status-message").textContent = state.lastDispatchMessage;
        renderTruckStatuses(state.trucks);
        if (!routingUpdatePending && state.routingAlgorithm) {
            routeAlgorithmSelect.value = state.routingAlgorithm;
        }

        if (state.chargingStations && Array.isArray(state.chargingStations)) {
            state.chargingStations.forEach((station) => {
                const marker = chargingMarkers[station.nodeId];
                if (!marker) return;
                const queueText = station.waitingQueue.length
                    ? station.waitingQueue.join(", ")
                    : "Empty";
                marker.setPopupContent(`
                    <div>
                        <b>Charging Station (Node ${station.nodeId})</b><br>
                        <b>Occupied bays:</b> ${station.occupiedBays} / ${station.bayCapacity}<br>
                        <b>FIFO waiting queue:</b> ${queueText}
                    </div>
                `);
            });
        }

        maintenanceLayer.clearLayers();
        if (state.roadMaintenance && Array.isArray(state.roadMaintenance)) {
            state.roadMaintenance.forEach((road) => {
                if (!road.geometry || road.geometry.length < 2) return;
                L.polyline(road.geometry, {
                    color: "#dc2626",
                    weight: 7,
                    opacity: 0.9,
                    dashArray: "8, 6"
                }).bindPopup(
                    `Road ${road.from}-${road.to} is under maintenance`
                ).addTo(maintenanceLayer);
            });
        }

        routeLayer.clearLayers();

        for (const t of state.trucks) {
            const shortNum = t.id.replace("TRK-", "");
            if (!truckMarkers[t.id]) {
                const marker = L.marker([t.lat, t.lon], {
                    icon: createCircleIcon("#facc15", 20, "#111827", shortNum),
                    zIndexOffset: 500
                }).addTo(truckLayer);
                marker.bindPopup(formatTruckPopup(t));
                truckMarkers[t.id] = marker;
                truckTargets[t.id] = { lat: t.lat, lon: t.lon };
            } else {
                truckTargets[t.id] = { lat: t.lat, lon: t.lon };
                truckMarkers[t.id].setPopupContent(formatTruckPopup(t));
            }

            if (t.activeRoute && t.activeRoute.length >= 2) {
                let routeColor = "#eab308";
                if (t.status === "Returning") routeColor = "#6d28d9";
                else if (t.status === "GoingToCharge") routeColor = "#0284c7";
                L.polyline(t.activeRoute, {
                    color: routeColor,
                    weight: 4,
                    opacity: 0.75,
                    dashArray: "6, 6"
                }).addTo(routeLayer);
            }
        }

        for (const r of state.requests) {
            const isCleaned = r.status === "Cleaned";
            if (isCleaned && expiredCollectedMarkers.has(r.requestCode)) continue;
            const color = isCleaned ? "#16a34a" : "#8b4513";

            if (!requestMarkers[r.requestCode]) {
                const marker = L.marker([r.lat, r.lon], {
                    icon: createCircleIcon(color, 16, "#ffffff")
                }).addTo(requestLayer);
                marker.bindPopup(formatRequestPopup(r));
                marker._lastStatus = r.status;
                requestMarkers[r.requestCode] = marker;
                if (isCleaned) {
                    scheduleCollectedMarkerRemoval(r.requestCode, r.cleanedAtUnixMs);
                }
            } else {
                const marker = requestMarkers[r.requestCode];
                if (marker._lastStatus !== r.status) {
                    marker.setIcon(createCircleIcon(color, 16, "#ffffff"));
                    marker._lastStatus = r.status;
                    if (isCleaned) {
                        scheduleCollectedMarkerRemoval(r.requestCode, r.cleanedAtUnixMs);
                    }
                }
                marker.setPopupContent(formatRequestPopup(r));
            }
        }
    } catch (err) {
        console.error("Failed to poll state:", err);
    }
}

function animateTrucks() {
    for (const id of Object.keys(truckMarkers)) {
        const marker = truckMarkers[id];
        const target = truckTargets[id];
        if (!target) continue;

        const curr = marker.getLatLng();
        const nextLat = curr.lat + (target.lat - curr.lat) * 0.35;
        const nextLon = curr.lng + (target.lon - curr.lng) * 0.35;
        marker.setLatLng([nextLat, nextLon]);
    }
    requestAnimationFrame(animateTrucks);
}

map.on("click", async (e) => {
    try {
        if (mapActionSelect.value !== "report") {
            const response = await fetch("/api/maintenance", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({
                    lat: e.latlng.lat,
                    lon: e.latlng.lng,
                    closed: mapActionSelect.value === "close-road"
                })
            });
            const result = await response.json();
            if (!response.ok) {
                throw new Error(result.error || "Failed to update road maintenance");
            }
            await pollState();
            return;
        }

        const res = await fetch("/api/request", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                lat: e.latlng.lat,
                lon: e.latlng.lng,
                quantity: 35
            })
        });
        if (res.ok) {
            await pollState();
        }
    } catch (err) {
        console.error("Failed to process map click:", err);
        document.getElementById("status-message").textContent = err.message;
    }
});

loadRoadNetwork();
pollState();
setInterval(pollState, 150);
requestAnimationFrame(animateTrucks);
