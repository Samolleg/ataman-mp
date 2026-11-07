// 288: only immutable worker values cross this callback. No editor/document call enters the DLL.
int pump_output_display(Session& s,probe1b_error* error) {
 if(!s.map||!s.liveValues||s.independentOutput)return 0;
 const auto now=std::chrono::steady_clock::now();if(now-s.livePump<std::chrono::milliseconds(24))return 0;
 EirCamera camera{};EirService service{};
 if(!s.liveValues(s.presentContext,s.generation,s.frameCamera.neighbours_visible,&camera,&service))return fail(error,E_ABORT,"offscreen cancelled: document or visibility changed");
 EirError detail{};if(const auto rc=host_camera(&s,&camera,&detail))return fail(error,rc,detail.message);
 if(const auto rc=host_service(&s,&service,&detail))return fail(error,rc,detail.message);
 const auto rc=map_frame(s,error);s.livePump=std::chrono::steady_clock::now();return rc;
}
