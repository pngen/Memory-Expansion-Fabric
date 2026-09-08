
function(mef_add_example name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE mef_core)
  mef_apply_warnings(${name})
endfunction()

mef_add_example(example_basic)
mef_add_example(example_selection)
mef_add_example(example_reservation)
mef_add_example(example_drain)
mef_add_example(example_persistence)
mef_add_example(example_synthetic)
mef_add_example(example_system)
