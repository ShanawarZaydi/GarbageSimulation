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

const truckMarkers = {};
const truckTargets = {};
const requestMarkers = {};
let disposalMarker = null;

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
                const routeColor = t.status === "Returning" ? "#6d28d9" : "#eab308";
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
            const color = isCleaned ? "#16a34a" : "#8b4513";

            if (!requestMarkers[r.requestCode]) {
                const marker = L.marker([r.lat, r.lon], {
                    icon: createCircleIcon(color, 16, "#ffffff")
                }).addTo(requestLayer);
                marker.bindPopup(formatRequestPopup(r));
                marker._lastStatus = r.status;
                requestMarkers[r.requestCode] = marker;
            } else {
                const marker = requestMarkers[r.requestCode];
                if (marker._lastStatus !== r.status) {
                    marker.setIcon(createCircleIcon(color, 16, "#ffffff"));
                    marker._lastStatus = r.status;
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
        console.error("Failed to submit garbage request:", err);
    }
});

loadRoadNetwork();
pollState();
setInterval(pollState, 150);
requestAnimationFrame(animateTrucks);
