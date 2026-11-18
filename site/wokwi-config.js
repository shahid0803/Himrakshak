// Public Render API used by the static dashboard. For local development,
// replace with http://127.0.0.1:8765 or an empty string for same-origin.
window.HIMRAKSHAK_API_BASE = "https://himrakshak.onrender.com";

// Public Wokwi project URL. The iframe is view-only; commands flow through
// the backend to the ESP32 firmware's remote polling endpoint.
window.HIMRAKSHAK_WOKWI = {
  "HR-01": "https://wokwi.com/projects/480976780970275841?embed=1",
  "HR-02": "",
  "HR-03": ""
};
