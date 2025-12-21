'''
Test Color Monitor plugin
'''

import time
import unittest
from onsdriver import obstest, obsui


class ColorMonitorTest(obstest.OBSTest):
    'Test plain features of OBS Studio'

    def setUp(self, config_name='saved-config', run=True):
        super().setUp(run=False, config_name=config_name)

        if run:
            self.obs.run()

    def test_dock(self):
        'Create the dock'
        cl = self.obs.get_obsws()
        ui = obsui.OBSUI(cl)

        scene = 'Scene'
        name = 'Color Source'
        cl.send('CreateInput', {
            'inputName': name,
            'sceneName': scene,
            'inputKind': 'color_source_v3',
            'inputSettings': {
                'color': 0xFFD7CCC6,
                'width': 640,
                'height': 360,
            },
        })

        ui.request('menu-trigger', {
            'path': [
                {"text": "&Tools"},
                {"text": "New Scope Dock...", "objectName": "actionScopeDockNew"},
            ]
        })

        ui.request('widget-invoke', {
            'path': [
                {"className": "ScopeDockNewDialog"},
                {"className": "QDialogButtonBox"},
                {"className": "QPushButton", "default": True},
            ],
            'method': 'click',
        })

        time.sleep(1)

        dock_path = [
            {"className": "OBSDock"},
            {"className": "ScopeWidget"},
        ]

        ui.grab(path=dock_path, filename=f'screenshots/{self.name}-window.png', window=True)

        try:
            ui.grab(path=dock_path, filename=f'screenshots/{self.name}-pillow.png', pillow=True)
        except (ImportError, OSError) as e:
            # OSError: X connection failed on Linux if DISPLAY is not set.
            print(e)


if __name__ == '__main__':
    unittest.main()
