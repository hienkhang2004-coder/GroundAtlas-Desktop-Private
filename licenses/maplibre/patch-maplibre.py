"""Small, recorded fixes for the pinned MapLibre Native for Qt 3.0.0 SDK."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
assert (root / "VERSION.txt").read_text().strip() == "3.0.0"

def replace(path, before, after):
    text = path.read_text(encoding="utf-8")
    if after in text:
        return
    if text.count(before) != 1:
        raise RuntimeError(f"Unexpected upstream source: {path}")
    path.write_text(text.replace(before, after), encoding="utf-8", newline="\n")

logging = root / "vendor/maplibre-native/src/mbgl/util/logging.cpp"
replace(logging, "std::atomic<bool> useThread(true);", """// GroundAtlas: Windows terminates other threads before DLL static destructors.
// A process-lifetime log scheduler then hangs in MinGW pthread_cond_destroy.
// Qt's platform logger is already thread-safe; use it synchronously on Windows.
#ifdef _WIN32
std::atomic<bool> useThread(false);
#else
std::atomic<bool> useThread(true);
#endif""")
replace(logging, """    Impl()
        : scheduler(Scheduler::GetSequenced()) {}""", "    Impl() = default;")
replace(logging, """        if (useThread) {
            auto threadName = platform::getCurrentThreadName();""", """        if (useThread) {
            // Do not create an unused worker when synchronous logging is selected.
            const std::lock_guard<std::mutex> lock(schedulerMutex);
            if (!scheduler) scheduler = Scheduler::GetSequenced();
            auto threadName = platform::getCurrentThreadName();""")
replace(logging, "    const std::shared_ptr<Scheduler> scheduler;", "    std::shared_ptr<Scheduler> scheduler;\n    std::mutex schedulerMutex;")

http = root / "vendor/maplibre-native/platform/qt/src/mbgl/http_file_source.cpp"
replace(http, "#include <QByteArray>", "#include <QByteArray>\n#include <QDebug>")
replace(http, "    data.first = m_manager->get(networkRequest);", """#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    networkRequest.setTransferTimeout(15000);
#endif
    if (qEnvironmentVariableIsSet("GA_NATIVE_NETWORK_TRACE")) qInfo() << "basemap request:" << url;
    data.first = m_manager->get(networkRequest);""")
replace(http, "    QByteArray data = reply->readAll();", """    QByteArray data = reply->readAll();
    if (qEnvironmentVariableIsSet("GA_NATIVE_NETWORK_TRACE"))
        qInfo() << "basemap reply:" << url << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                << reply->error() << data.size() << reply->errorString();""")
# Keep the tiled renderer, picking inverse, culling and site mesh in one camera.
# This is a GroundAtlas SDK extension, not an upstream Qt binding API.
core = root / "vendor/maplibre-native"
replace(core / "include/mbgl/map/map.hpp", "#include <mbgl/map/projection_mode.hpp>", "#include <mbgl/map/projection_mode.hpp>\n#include <mbgl/util/mat4.hpp>")
replace(core / "include/mbgl/map/map.hpp", "    // Projection\n", "    // GroundAtlas: optional world-pixel/metre to clip projection.\n    void setCustomProjection(const std::optional<mat4>&);\n\n    // Projection\n")
replace(core / "src/mbgl/map/transform_state.hpp", "    // Dimensions\n", "    void setCustomProjection(const std::optional<mat4>& value) { customProjection = value; requestMatricesUpdate = true; }\n\n    // Dimensions\n")
replace(core / "src/mbgl/map/transform_state.hpp", "    mutable mat4 projectionMatrix;", "    std::optional<mat4> customProjection;\n    mutable mat4 projectionMatrix;")
state_path = core / "src/mbgl/map/transform_state.cpp"
state_text = state_path.read_text(encoding="utf-8")
state_text = state_text.replace("    if (customProjection) { projMatrix = *customProjection; return; }", "    if (customProjection) { updateCameraState(); projMatrix = *customProjection; return; }")
state_path.write_text(state_text, encoding="utf-8", newline="\n")
replace(core / "src/mbgl/map/transform_state.cpp", "void TransformState::getProjMatrix(mat4& projMatrix, uint16_t nearZ, bool aligned) const {", """void TransformState::getProjMatrix(mat4& projMatrix, uint16_t nearZ, bool aligned) const {
    // Shared by rendering, inverse screen coordinates and tile coverage.
    if (customProjection) { updateCameraState(); projMatrix = *customProjection; return; }""")
replace(core / "src/mbgl/map/transform.hpp", "    const TransformState& getState() const { return state; }", "    const TransformState& getState() const { return state; }\n    void setCustomProjection(const std::optional<mat4>& value) { state.setCustomProjection(value); }")
replace(core / "src/mbgl/map/map.cpp", "// MARK: - Projection\n", """void Map::setCustomProjection(const std::optional<mat4>& value) {
    impl->transform.setCustomProjection(value);
    impl->onUpdate();
}

// MARK: - Projection
""")
replace(root / "src/core/map.hpp", "    [[nodiscard]] QPointF pixelForCoordinate(const Coordinate &coordinate) const;", "    // GroundAtlas SDK extension: an empty vector restores the standard camera.\n    bool setCustomProjection(const QVector<double> &matrix);\n    [[nodiscard]] QPointF pixelForCoordinate(const Coordinate &coordinate) const;")
replace(root / "src/core/map.cpp", "QPointF Map::pixelForCoordinate(const Coordinate &coordinate) const {", """bool Map::setCustomProjection(const QVector<double> &values) {
    std::optional<mbgl::mat4> value;
    if (!values.isEmpty()) {
        if (values.size() != 16) return false;
        mbgl::mat4 matrix, inverse;
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(values[i])) return false;
            matrix[i] = values[i];
        }
        if (mbgl::matrix::invert(inverse, matrix)) return false;
        value = matrix;
    }
    if (!value) d_ptr->mapObj->jumpTo(mbgl::CameraOptions().withPitch(std::min(60.0, pitch())));
    d_ptr->mapObj->setCustomProjection(value);
    d_ptr->mapObj->setBounds(mbgl::BoundOptions().withMaxPitch(value ? 85 : 60));
    return true;
}

QPointF Map::pixelForCoordinate(const Coordinate &coordinate) const {""")
replace(core / "src/mbgl/map/transform_state.hpp", "    // North Orientation\n", "    double maximumProjectionPitch() const { return customProjection ? 85 * util::DEG2RAD_D : util::PITCH_MAX; }\n\n    // North Orientation\n")
replace(core / "src/mbgl/map/transform_state.cpp", "maxPitch = util::clamp(pitch_, minPitch, util::PITCH_MAX);", "maxPitch = util::clamp(pitch_, minPitch, maximumProjectionPitch());")
replace(core / "src/mbgl/map/transform.cpp", "if (maxPitch * util::DEG2RAD_D > util::PITCH_MAX)", "if (maxPitch * util::DEG2RAD_D > state.maximumProjectionPitch())")
replace(core / "src/mbgl/map/transform.cpp", "std::to_string(util::PITCH_MAX * util::RAD2DEG_D)", "std::to_string(state.maximumProjectionPitch() * util::RAD2DEG_D)")
print("Applied pinned logging, bounded HTTP and shared camera projection patches.")
