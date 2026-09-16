@echo off
rem =============================================================================
rem terrain_presets.bat
rem   地形のプリセット。使いたい行の rem を外して実行すると assets/terrain.obj が
rem   その地形に置き換わる (実行ファイルの再ビルドは不要、再起動で反映)。
rem =============================================================================
cd /d "%~dp0\.."

rem ---- 富士山 (山頂中心 8 km 四方、高さ誇張なし、投入は南斜面の山頂直下) ----
python tools\fetch_gsi_terrain.py --lat 35.3606 --lon 138.7274 --size 8 --exaggeration 1.0 --emit-lat 35.355 --emit-lon 138.729

rem ---- 関ヶ原 (6 km 四方、高さ 2 倍、投入は北側斜面の谷) ----
rem python tools\fetch_gsi_terrain.py --lat 35.362 --lon 136.472 --size 6 --exaggeration 2.0 --emit-lat 35.384 --emit-lon 136.470

rem ---- 試作用の谷 (Blender なしで生成) ----
rem python tools\make_test_valley.py
rem del assets\terrain.cfg
