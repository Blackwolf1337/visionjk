// ---- the weather of a map (tr_WorldEffects.cpp of the renderer): snow, rain, dust and the fog that drifts
//
// The game names the weather of a map among its effects, with a * in front (fx_snow, fx_rain and fx_spacedust of
// g_misc.c put them there), and the client hands each to the renderer as a command (CG_ParseWeatherEffect). The
// boxes of misc_weather_zone say where it is looked up whether a point is under the sky.
// Stand-in: there is none yet.

// at the end of the scene, after everything else of it is drawn
function drawWeather(now) {}
