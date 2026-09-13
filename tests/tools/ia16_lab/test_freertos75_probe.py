import unittest

from tools.ia16_lab.freertos75_probe import matching_map_lines, matching_symbols
from tools.ia16_lab.symbols import SymbolTable


class FreeRTOS75ProbeTests(unittest.TestCase):
    def test_matches_required_map_symbols(self) -> None:
        text = (
            "  1234:0010       _pxCurrentTCB\n"
            "  1234:0020       _xSuspendedTaskList\n"
            "  1234:0040       _pxReadyTasksLists\n"
            "  1000:0100       _uxListRemove\n"
            "  1000:0200       _prvAddCurrentTaskToDelayedList\n"
        )
        table = SymbolTable.from_wlink_map(text)
        names = {symbol.name for symbol in matching_symbols(table)}
        self.assertEqual(
            names,
            {
                "_pxCurrentTCB",
                "_xSuspendedTaskList",
                "_pxReadyTasksLists",
                "_uxListRemove",
                "_prvAddCurrentTaskToDelayedList",
            },
        )
        self.assertEqual(len(matching_map_lines(text)), 5)

    def test_raw_line_matching_preserves_unparsed_wlink_evidence(self) -> None:
        text = (
            "not-a-symbol line mentioning xSuspendedTaskList\n"
            "  1000:0000       _entry\n"
        )
        self.assertEqual(
            matching_map_lines(text),
            ("not-a-symbol line mentioning xSuspendedTaskList",),
        )


if __name__ == "__main__":
    unittest.main()
