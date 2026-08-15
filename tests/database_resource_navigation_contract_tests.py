#!/usr/bin/env python3
"""Regression contract for bidirectional Database Editor / Resource Browser navigation."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, message: str) -> None:
    if needle not in text:
        raise AssertionError(message)


def main() -> int:
    main_window = (ROOT / "source/app/main_window.cpp").read_text(encoding="utf-8")
    browser_h = (ROOT / "source/ui/resource_browser.h").read_text(encoding="utf-8")
    browser_cpp = (ROOT / "source/ui/resource_browser.cpp").read_text(encoding="utf-8")
    database_h = (ROOT / "source/ui/database_editor_dialog.h").read_text(encoding="utf-8")
    database_cpp = (ROOT / "source/ui/database_editor_dialog.cpp").read_text(encoding="utf-8")

    require(main_window, "openDatabaseArchiveFromBrowser(archivePath)",
            "Resource Browser database requests must invoke the real database-opening route.")
    if "A dedicated Database Editor integration request was created" in main_window:
        raise AssertionError("The dead pre-integration placeholder handler is still present.")
    require(browser_h, "bool selectResourceReference(const QString& reference)",
            "Resource Browser must expose exact resource-reference navigation.")
    require(browser_cpp, "database.res", "Database archive classification must be explicit.")
    require(browser_cpp, "m_databaseEditor->setVisible(isDedicatedDatabaseArchive(selectedArchivePath()))",
            "Selecting the database.res archive itself must expose the Database Editor action.")
    require(browser_cpp, "emit databaseEditorRequested(archive)",
            "The action must work from an archive selection, not only a member preview.")
    require(browser_cpp, "item->data(0, kEntryRole).toInt() != visibleEntryIndex",
            "Reference navigation must map through each sorted visual item's stable catalog index.")
    if "m_resources->topLevelItem(row);\n            m_resources->setCurrentItem(item)" in browser_cpp:
        raise AssertionError("Reference navigation still treats an unsorted catalog row as a visual row.")
    classifier = browser_cpp[browser_cpp.index("bool isDedicatedDatabaseArchive"):
                             browser_cpp.index("QString suffixType")]
    if "databaseJmv.res" in classifier:
        raise AssertionError("databaseJmv.res is a helper archive, not the editable database.")
    require(database_h, "void resourceBrowserRequested(const QString& reference)",
            "Database Editor must expose resource-reference navigation.")
    require(database_cpp, "databaseOpenResourceAction",
            "Known resource fields need an Open in Resource Browser context action.")
    require(database_cpp, "resourceReference",
            "The action must be schema-aware rather than offered for arbitrary cell text.")
    print("database/resource navigation contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"database/resource navigation contract: FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
