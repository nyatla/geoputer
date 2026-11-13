"""Check that benchmark preparation cannot change or contaminate normal sources."""
from pathlib import Path
import tempfile
import unittest
from benchmark_support import prepare_workspace, inject_storage_probe, diagnostic_config


class BenchmarkPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='rgp-benchmark-prepare-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root/'application').mkdir()
        (self.root/'application/main.cpp').write_text('normal application\n')
        (self.root/'tests/performance').mkdir(parents=True)
        (self.root/'tests/performance/Probe.h').write_text('probe\n')
        (self.root/'platformio.ini').write_text('[platformio]\nsrc_dir = application\n[env:test]\nbuild_flags =\n    -Wall\n')

    def test_isolation_and_repeated_preparation(self):
        work=prepare_workspace(self.root,'test-probe',['Probe.h'])
        (work/'application/OldComparison.cpp').write_text('stale variant')
        (work/'capture.log').write_text('previous measurements')
        (work/'application/main.cpp').write_text('instrumented copy')
        prepare_workspace(self.root,'test-probe',['Probe.h'])
        self.assertEqual((work/'application/main.cpp').read_text(),'normal application\n')
        self.assertFalse((work/'application/OldComparison.cpp').exists())
        self.assertEqual((self.root/'application/main.cpp').read_text(),'normal application\n')
        self.assertEqual((work/'capture.log').read_text(),'previous measurements')

    def test_probe_anchor_must_be_unique(self):
        source='#include "GpsStorageTask.h"\nvoid GpsStorageTask::run() {\n        vTaskDelay(pdMS_TO_TICKS(5));\n}\n'
        modified=inject_storage_probe(source,'Probe.h','        probe();\n')
        self.assertEqual(modified.count('probe();'),1)
        self.assertIn('probe();\n        vTaskDelay',modified)
        with self.assertRaises(RuntimeError):
            inject_storage_probe(source.replace('vTaskDelay','changedLoop'),'Probe.h','probe();')
        with self.assertRaises(RuntimeError):
            inject_storage_probe(source+source,'Probe.h','probe();')

    def test_diagnostic_config_leaves_normal_config_untouched(self):
        path=self.root/'platformio.ini';before=path.read_text()
        config=diagnostic_config(self.root,self.root/'.pio/test-probe')
        self.assertIn('-DGEOPUTER_DIAGNOSTICS=1',config)
        self.assertIn('build_dir = .pio/test-probe/build',config)
        self.assertEqual(path.read_text(),before)
        for name in ('../application','/tmp/application','with space'):
            with self.assertRaises(ValueError):
                prepare_workspace(self.root,name,[])


if __name__=='__main__':
    unittest.main()
