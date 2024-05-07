import path from 'path'
import waitForExpect from 'wait-for-expect'
import { loadModel } from '../lib'

it('work fine', async () => {
  let tokens = ''
  const model = await loadModel({ model: path.resolve(__dirname, './tiny-random-llama.gguf'), n_gpu_layers: 0 })
  const result = await model.completion({
    prompt: 'My name is Merve and my favorite',
    n_samples: 1,
    temperature: 0,
    max_length: 100,
    n_predict: 10,
    seed: 0,
  }, (data) => {
    expect(data).toMatchObject({ token: expect.any(String) })
    tokens += data.token
  })
  expect(result).toMatchSnapshot()
  await waitForExpect(() => {
    expect(tokens).toBe(result.text)
  })
  await model.saveSession(path.resolve(__dirname, './tmp.sess'))
  await model.loadSession(path.resolve(__dirname, './tmp.sess'))
  await model.release()
})
