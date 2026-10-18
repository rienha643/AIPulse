# AI Pulse

A compact native Windows taskbar widget for NVIDIA GPU load, VRAM, and system RAM. Built for keeping an eye on AI workloads such as ComfyUI without opening a full monitoring dashboard.

AI 작업 중 GPU와 RAM을 한눈에 확인하는 작은 Windows 작업 표시줄 위젯입니다.

## Features

- 여러 NVIDIA GPU의 이름, 부하, VRAM 사용량/전체 용량 표시
- RAM 사용률과 사용량/전체 용량 표시
- GPU별·항목별 표시 선택과 자동 폭 조정
- 기본 1초 갱신, 2초·5초 선택 가능
- 드래그, 위치 고정, 시스템 밝기 테마 대응, 선택적 자동 실행
- 숨긴 센서 조회 중단, 화면 비표시·잠금·절전 중 센서 수집 대기
- 드라이버 오류 후 재시도 및 GPU UUID 기반 재연결
- 설정을 유지하는 로컬 빌드 업데이트와 이전 실행 파일 복구

C/Win32와 GDI로 구현했습니다. NVIDIA 드라이버의 NVML을 직접 사용하며 GPU 연산·렌더링 컨텍스트를 직접 생성하지 않습니다. 앱 실행에 Python, Electron, .NET은 필요하지 않습니다. 화면 합성과 드라이버 조회 비용까지 0이라는 의미는 아닙니다.

## Requirements

- Windows x64와 NVIDIA GPU/드라이버 (`nvml.dll`은 드라이버에 포함)
- 빌드: PowerShell 7, Zig **0.15.2**

AMD·Intel GPU 모니터링은 지원하지 않습니다. 작업 표시줄 안에 공간을 예약하는 확장이 아니라 작업 표시줄 위에 배치되는 작은 창이므로, 아이콘이 많으면 위치를 조정해야 합니다.

## Build and run

저장소를 내려받은 폴더에서 PowerShell 7로 실행합니다.

```powershell
./setup-zig.ps1
./build.ps1

$runtime = Join-Path $env:LOCALAPPDATA 'AIPulse'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Copy-Item ./out/AIPulse.exe $runtime
Start-Process (Join-Path $runtime 'AIPulse.exe')
```

`setup-zig.ps1`은 공식 배포 파일을 다운로드하고 고정된 SHA-256 해시를 확인합니다. Zig 0.15.2가 PATH에 있다면 이 단계를 생략할 수 있습니다. 컴파일러는 저장소에 포함되지 않습니다.

실행 폴더에는 `AIPulse.exe`와 실행 시 생성되는 `AIPulse.ini`만 있으면 됩니다. 사용자가 쓸 수 있는 폴더에 두세요. 공개 저장소에는 개인 설정이나 빌드된 실행 파일이 포함되지 않습니다.

## Controls

위젯을 오른쪽 클릭하면 표시 항목, 위치 고정, 갱신 간격, 자동 실행을 설정할 수 있습니다. 위치 고정을 해제하면 드래그할 수 있습니다.

- GPU 부하: NVML 기준이며 작업 관리자와 정의·표본 시점이 다를 수 있습니다.
- 메모리의 `G`: GiB 단위입니다. 제품 표기 16GB와 드라이버가 보고하는 실제 전체 용량은 다를 수 있습니다.
- RAM 사용량: 전체 물리 RAM에서 사용 가능한 물리 RAM을 뺀 값입니다.
- 자동 실행은 기본 꺼짐이며 현재 사용자 Run 레지스트리에 등록합니다.

## Local update

앱을 한 번 실행한 뒤, 개발 폴더의 `update.cmd`를 더블클릭하세요. 빌드가 성공한 후 AI Pulse만 정상 종료하고 교체·재실행합니다. 결과 창은 키를 누를 때까지 유지됩니다.

기본 실행 폴더는 `%LOCALAPPDATA%\AIPulse`입니다. 다른 설치 경로는 추적되지 않는 `update.local.json`에 지정할 수 있습니다.

```json
{ "RuntimeDirectory": "D:/Apps/AIPulse" }
```

또는 `./update.ps1 -RuntimeDirectory 'D:/Apps/AIPulse'`를 사용하세요. `create-update-shortcut.ps1`은 현재 개발 폴더를 가리키는 더블클릭용 바로가기를 만듭니다.

업데이터는 설정 파일과 자동 실행 등록을 덮어쓰지 않습니다. 백업은 `backups/`, 로그와 마지막 결과는 `reports/`에 저장합니다. 새 앱 시작이 실패하면 이전 실행 파일로 복구를 시도합니다. PowerShell 실행 정책이나 보안 설정은 변경하지 않습니다.

## Verification

```powershell
./tests/test-updater.ps1
```

이 검사는 실행 중인 앱 대신 작은 임시 파일로 실행 파일 교체·복구·복사 실패·설정 보존을 확인합니다.

`tests/test-aipulse.c`는 실제 NVIDIA GPU 두 개가 있는 개발 환경에서 센서 선택, 비표시 대기, 모의 오류 후 재연결을 검사합니다. `tests/check-text-fit.c`는 여러 배율에서 표시 문자열의 폭을 확인합니다. GPU 드라이버를 실제로 재시작하는 테스트는 아닙니다.

`tests/test-taskbar-owner.c`는 화면 밖의 시험 창에서 작업 표시줄 역할의 창을 100회 올려도 위젯이 즉시 위에 유지되는지 검사합니다. 표시 창은 작업 표시줄의 소유 창으로 만들고, 독립적인 숨김 관리 창이 작업 표시줄 재생성 시 표시 창을 복구합니다. 이 방식은 작업 표시줄 클릭 후 다음 주기까지 가려지는 시간을 없애며 Explorer에 코드를 주입하지 않습니다.

개발 PC에서 수정 버전을 10분간 관찰했을 때 작업 집합은 약 31MiB, 전용 메모리는 약 21MiB였고, 핸들과 GDI 객체의 지속 증가를 관찰하지 못했습니다. 장치·드라이버·표시 상태에 따라 달라지며 수시간·수일 안정성 보증은 아닙니다.

## Development layout

소스와 개발 도구는 실행 폴더와 분리해서 보관합니다. `out/`, `tools/`, 설정·로그·백업·바로가기는 Git에서 제외합니다. 프로그램에는 네트워크 통신이나 자동 원격 업데이트 기능이 없습니다. 컴파일러 설치 스크립트만 명시적으로 공식 다운로드 서버에 접속합니다.
