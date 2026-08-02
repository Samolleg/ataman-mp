from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TerrainShadowContractTests(unittest.TestCase):
    """Focused regression for terrain never entering the sun shadow atlas."""

    def test_terrain_shadow_is_a_distinct_persisted_toggle(self):
        settings = (ROOT / "source/app/settings.cpp").read_text(encoding="utf-8")
        settings_ui = (ROOT / "source/app/settings.ui").read_text(encoding="utf-8")
        option_sets = (ROOT / "assets/resources/optSet.txt").read_text(encoding="utf-8")

        self.assertIn('COptBool("renderTerrainShadows"', settings)
        self.assertIn('name="renderTerrainShadows"', settings_ui)
        self.assertIn("renderTerrainShadows", option_sets)

    def test_terrain_uses_heightfield_visibility_not_shadow_atlas_self_comparison(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")
        shader = (ROOT / "assets/shaders/landshader.fsh").read_text(encoding="utf-8")

        self.assertNotIn("m_pLand->drawShadow", view)
        self.assertIn("terrainShadowsEnabled", view)
        self.assertIn("updateTerrainShadowVisibility", view)
        self.assertIn("traceTerrainSunVisibility", landscape)
        self.assertIn("u_terrainShadowMap", shader)
        self.assertIn("texture2D(u_terrainShadowMap", shader)

    def test_terrain_and_object_shadows_form_one_strongest_blocker_silhouette(self):
        shader = (ROOT / "assets/shaders/landshader.fsh").read_text(encoding="utf-8")

        self.assertIn("float combinedShadowVisibility = min(shadow,", shader)
        self.assertIn("clamp(terrainShadowVisibility(), 0.0, 1.0)", shader)
        self.assertNotIn("sunDiffuse\n                           * clamp(v_terrainSunVisibility", shader)
        self.assertEqual(shader.count("80.0 / 255.0"), 1)

    def test_terrain_field_rebuild_is_coalesced_off_the_render_thread(self):
        landscape_h = (ROOT / "source/render/landscape.h").read_text(encoding="utf-8")
        landscape_cpp = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        self.assertIn("QFuture<STerrainShadowFieldResult>", landscape_h)
        self.assertIn("QtConcurrent::run", landscape_cpp)
        self.assertIn("m_terrainShadowDesiredGeneration", landscape_h)
        self.assertIn("m_terrainShadowFutureGeneration", landscape_h)
        self.assertIn("m_terrainShadowFuture.isFinished()", landscape_cpp)
        self.assertIn("uploadTerrainShadowTexture", landscape_cpp)

    def test_sculpt_stroke_retains_one_complete_shadow_snapshot(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        update_call = view.split("updateTerrainShadowVisibility(", 1)[1].split(");", 1)[0]
        self.assertIn("!m_sculptStrokeActive", update_call)
        update = landscape.split(
            "void CLandscape::updateTerrainShadowVisibility", 1)[1].split(
                "void CLandscape::drawWater", 1)[0]
        self.assertIn("if(!allowRebuild)", update)
        self.assertLess(update.index("if(!allowRebuild)"),
                        update.index("QtConcurrent::run"))
        self.assertIn("allowRebuild && m_terrainShadowDesiredEnabled", update)

    def test_terrain_generation_and_toggle_invalidate_visibility_field(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.h").read_text(encoding="utf-8")

        self.assertIn("shadowGeneration", landscape)
        self.assertIn("terrainShadowsEnabled", view)
        self.assertIn("m_terrainShadowFieldGeneration", landscape)

    def test_only_height_and_overlay_coverage_edits_invalidate_terrain_field(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape_h = (ROOT / "source/render/landscape.h").read_text(encoding="utf-8")
        landscape_cpp = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")
        terrain_model = (ROOT / "source/documents/terrain_edit_model.h").read_text(
            encoding="utf-8")

        sector_preview = landscape_cpp.split(
            "bool CLandscape::previewCanonicalSector", 1)[1].split(
                "bool CLandscape::previewCanonicalOverlaySector", 1)[0]
        tile_preview = view.split(
            "bool CView::applyTerrainTilePreview", 1)[1].split(
                "bool CView::applyOverlaySurfacePreview", 1)[0]
        overlay_preview = view.split(
            "bool CView::applyOverlaySurfacePreview", 1)[1].split(
                "void CView::beginTerrainPreviewBatch", 1)[0]
        height_preview = view.split(
            "bool CView::applyTerrainHeightPreview", 1)[1].split(
                "bool CView::applyTerrainVerticalScalePreview", 1)[0]

        self.assertIn("invalidateTerrainShadowField", landscape_h)
        self.assertNotIn("++m_shadowGeneration", sector_preview)
        self.assertNotIn("invalidateTerrainShadowField", tile_preview)
        self.assertIn("invalidateTerrainShadowField", height_preview)
        self.assertIn("affectsTerrainShadowField", terrain_model)
        self.assertIn("command.affectsTerrainShadowField", overlay_preview)
        self.assertIn("invalidateTerrainShadowField", overlay_preview)

    def test_editor_only_trap_and_helper_visuals_never_enter_shadow_queue(self):
        magictrap = (ROOT / "source/objects/magictrap.h").read_text(encoding="utf-8")
        node = (ROOT / "source/core/node.h").read_text(encoding="utf-8")

        self.assertIn(
            "bool castsRenderShadow() const override { return false; }", magictrap)
        self.assertIn("virtual bool castsRenderShadow() const { return false; }", node)

    def test_static_shadow_cache_depends_only_on_actual_casters(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        render_queue = (ROOT / "source/render/render_queue.cpp").read_text(
            encoding="utf-8")

        static_hash = view.split("QCryptographicHash staticHash", 1)[1].split(
            "bool rebuildStatic", 1)[0]
        self.assertNotIn("sceneSourceFingerprint", static_hash)
        self.assertIn("queue.staticShadowFingerprint()", view)
        self.assertIn("staticHash.addData(staticCasterFingerprint)", static_hash)
        self.assertIn("farStaticHash.addData(staticCasterFingerprint)", static_hash)
        self.assertIn("item.instanceMatrix.constData()", render_queue)

    def test_static_shadow_fingerprint_is_independent_of_submission_order(self):
        render_queue = (ROOT / "source/render/render_queue.cpp").read_text(
            encoding="utf-8")

        fingerprint = render_queue.split(
            "QByteArray RenderQueueContext::staticShadowFingerprint() const", 1)[1].split(
                "void RenderQueueContext::flushShadow", 1)[0]
        self.assertIn("QVector<QByteArray> casterRecords", fingerprint)
        self.assertIn("std::sort(casterRecords.begin(), casterRecords.end())", fingerprint)

    def test_selection_never_rewrites_shadow_spatial_bounds(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        rebuild = view.split("void CView::rebuildSpatialIndices()", 1)[1].split(
            "QVector<CNode*> CView::spatiallyVisibleNodes", 1)[0]
        self.assertIn("grid->insert(node, center, radius)", rebuild)
        self.assertNotIn("const bool selected", rebuild)
        self.assertNotIn("if(selected", rebuild)

        self.assertNotIn("connect(this, &CView::selectionChanged", view)
        undo_hook = view.split("connect(m_pUndoStack", 1)[1].split(
            "m_pProgress = pProgress", 1)[0]
        selection_undo = undo_hook.split(
            "if(!selectionOnly)", 1)[1].split("}", 1)[0]
        self.assertIn("m_spatialIndexDirty = true", selection_undo)

    def test_sector_geometry_never_owns_terrain_shadow_visibility(self):
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")
        sector_h = (ROOT / "source/render/sector.h").read_text(encoding="utf-8")
        sector_cpp = (ROOT / "source/render/sector.cpp").read_text(encoding="utf-8")
        vertex = (ROOT / "assets/shaders/landshader.vsh").read_text(encoding="utf-8")

        preview = landscape.split(
            "bool CLandscape::previewCanonicalSector", 1)[1].split(
                "bool CLandscape::previewCanonicalOverlaySector", 1)[0]
        self.assertNotIn("m_terrainShadowFieldEnabled", preview)
        self.assertNotIn("applyTerrainShadowVisibility", preview)
        self.assertNotIn("shadowVisibility", sector_h)
        self.assertNotIn("applyTerrainShadowVisibility", sector_cpp)
        self.assertNotIn("a_terrainSunVisibility", vertex)

    def test_terrain_shadow_publication_is_an_atomic_texture_swap(self):
        landscape_h = (ROOT / "source/render/landscape.h").read_text(encoding="utf-8")
        landscape_cpp = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        self.assertIn("QOpenGLTexture* m_terrainShadowTexture", landscape_h)
        upload = landscape_cpp.split(
            "bool CLandscape::uploadTerrainShadowTexture", 1)[1].split(
                "void CLandscape::updateTerrainShadowVisibility", 1)[0]
        self.assertIn("QOpenGLTexture* candidate", upload)
        self.assertLess(upload.index("candidate->setData"),
                        upload.index("m_terrainShadowTexture = candidate"))

    def test_static_shadow_atlas_has_no_unproven_publication_debounce(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        cascade = (ROOT / "source/render/shadow_cascade.h").read_text(encoding="utf-8")

        self.assertNotIn("ShadowCachePublicationGate", cascade)
        self.assertNotIn("kStaticShadowSettleMs", view)
        self.assertNotIn("m_staticShadowPublicationGate", view)

    def test_editor_query_passes_never_clear_the_visible_framebuffer(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        view_h = (ROOT / "source/render/view.h").read_text(encoding="utf-8")

        rectangle_pick = view.split(
            "void CView::pickObject(const QRect &rect", 1)[1].split(
                "QVector3D CView::getTerrainPos(bool", 1)[0]
        point_pick = view.split(
            "CNode* CView::pickObject(QList<CNode*>& aNode", 1)[1].split(
                "QVector3D CView::getTerrainPos(const int", 1)[0]
        terrain_pick = view.split(
            "QVector3D CView::getTerrainPos(const int", 1)[1].split(
                "void CView::changeOperation", 1)[0]

        self.assertIn("m_editorQueryFbo", view_h)
        for query_pass, read_marker in (
                (rectangle_pick, "getColorFromRect"),
                (point_pick, "ReadPixels"),
                (terrain_pick, "ReadPixels")):
            self.assertIn("beginEditorQueryFramebuffer", query_pass)
            self.assertIn("endEditorQueryFramebuffer", query_pass)
            self.assertLess(query_pass.index("beginEditorQueryFramebuffer"),
                            query_pass.index("Clear(GL_COLOR_BUFFER_BIT"))
            self.assertLess(query_pass.index(read_marker),
                            query_pass.rindex("endEditorQueryFramebuffer"))

    def test_disabling_toggle_immediately_stops_sampling_terrain_texture(self):
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        disabled = landscape.index("if(!enabled)")
        enabled = landscape.index("const QVector3D normalized", disabled)
        transition = landscape[disabled:enabled]
        self.assertIn("m_terrainShadowFieldEnabled = false", transition)
        self.assertNotIn("fill(1.0f", transition)
        self.assertNotIn("applyTerrainShadowVisibility", transition)

    def test_angle_and_minimap_safeguards_remain_fail_closed(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("!m_openGlesCompatibility", view)
        self.assertIn("!m_minimapCaptureActive", view)
        self.assertIn("terrainShadowsEnabled", view)

    def test_terrain_shadow_path_has_no_depth_bias(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        vertex = (ROOT / "assets/shaders/vshader.vsh").read_text(encoding="utf-8")

        self.assertNotIn("u_shadowDepthBias", vertex)
        self.assertNotIn("GL_POLYGON_OFFSET_FILL", view)

    def test_map_weather_palette_uses_authored_allod_not_zone_type(self):
        main_window = (ROOT / "source/app/main_window.cpp").read_text(encoding="utf-8")

        self.assertIn("setMapSkyType(m_mapTestEntry.skyType, m_mapTestEntry.allodId)",
                      main_window)

    def test_sun_rotation_and_terrain_length_match_game_proxy_contract(self):
        environment = (ROOT / "source/render/render_environment.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        azimuth_start = environment.index("const float azimuth")
        azimuth_end = environment.index(";", azimuth_start)
        self.assertNotIn("+ float(M_PI)", environment[azimuth_start:azimuth_end])
        self.assertIn("kTerrainShadowLengthScale", landscape)
        self.assertIn("/ kTerrainShadowLengthScale", landscape)


if __name__ == "__main__":
    unittest.main()
